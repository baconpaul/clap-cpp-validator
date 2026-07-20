/*
 * clap-cpp-validator: A re-implementation of the RUST clap validator
 * in c++
 *
 * Copyright 2026, various authors, as described in the GitHub
 * transaction log.
 *
 * This code is licensed under the MIT software licensed. It is
 * initiated by using Claude Sonnet to port the equivalent but
 * no longer actively developed RUST validator.
 *
 * All source in sst-filters available at
 * https://github.com/baconpaul/clap-cpp-validator
 */
#include "validate.h"
#include "../plugin/library.h"
#include "../tests/plugin_library_tests.h"
#include "../tests/plugin_tests.h"
#include "../util.h"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace clap_validator
{
namespace commands
{

namespace
{
// Convert a status token (as produced by statusCodeToString) back into a TestStatusCode.
TestStatusCode statusCodeFromString(const std::string &token)
{
    if (token == "success")
        return TestStatusCode::Success;
    if (token == "crashed")
        return TestStatusCode::Crashed;
    if (token == "failed")
        return TestStatusCode::Failed;
    if (token == "skipped")
        return TestStatusCode::Skipped;
    if (token == "warning")
        return TestStatusCode::Warning;
    return TestStatusCode::Failed;
}

// Escape a string for embedding in a JSON string literal. Detail strings can contain quotes and,
// now that mismatch lists are multi-line, newlines - both of which would otherwise produce invalid
// JSON.
std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

// RAII helper that redirects stdout and stderr to /dev/null for its lifetime, used to hush a
// plugin's chatter while the parent loads a library to enumerate its plugins. No-op on Windows.
class OutputSilencer
{
  public:
    explicit OutputSilencer(bool enabled = true)
    {
        if (!enabled)
        {
            return;
        }
#ifndef _WIN32
        std::cout.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        savedOut_ = dup(STDOUT_FILENO);
        savedErr_ = dup(STDERR_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
#endif
    }

    ~OutputSilencer()
    {
#ifndef _WIN32
        std::fflush(stdout);
        std::fflush(stderr);
        if (savedOut_ >= 0)
        {
            dup2(savedOut_, STDOUT_FILENO);
            close(savedOut_);
        }
        if (savedErr_ >= 0)
        {
            dup2(savedErr_, STDERR_FILENO);
            close(savedErr_);
        }
#endif
    }

    OutputSilencer(const OutputSilencer &) = delete;
    OutputSilencer &operator=(const OutputSilencer &) = delete;

  private:
#ifndef _WIN32
    int savedOut_ = -1;
    int savedErr_ = -1;
#endif
};

// Serialize a result to the inter-process result file. The format is deliberately simple and
// self-delimiting (status line, name line, a details marker line, then the raw details) so
// arbitrary multi-line detail strings need no escaping.
void writeResultFile(const std::filesystem::path &file, const TestResult &result)
{
    std::ofstream os(file, std::ios::binary | std::ios::trunc);
    os << statusCodeToString(result.status) << "\n";
    os << result.name << "\n";
    if (result.details)
    {
        os << "D\n" << *result.details;
    }
    else
    {
        os << "N\n";
    }
}

// Read a result written by writeResultFile back. Returns nullopt if the file is missing or
// malformed. The description is not transmitted (it is not shown), so the caller supplies it.
std::optional<TestResult> readResultFile(const std::filesystem::path &file,
                                         const std::string &description)
{
    std::ifstream is(file, std::ios::binary);
    if (!is)
    {
        return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());

    size_t p1 = content.find('\n');
    if (p1 == std::string::npos)
        return std::nullopt;
    size_t p2 = content.find('\n', p1 + 1);
    if (p2 == std::string::npos)
        return std::nullopt;
    size_t p3 = content.find('\n', p2 + 1);
    if (p3 == std::string::npos)
        return std::nullopt;

    std::string statusStr = content.substr(0, p1);
    std::string name = content.substr(p1 + 1, p2 - p1 - 1);
    std::string marker = content.substr(p2 + 1, p3 - p2 - 1);

    TestResult result;
    result.name = name;
    result.description = description;
    result.status = statusCodeFromString(statusStr);
    if (marker == "D")
    {
        result.details = content.substr(p3 + 1);
    }
    return result;
}

// Run one check in a child process and read back its result. If the child exits abnormally (a
// crashing plugin), returns a Crashed result. Never called on Windows.
TestResult runTestOutOfProcess(const ValidatorSettings &settings, TestKind kind,
                               const std::filesystem::path &path, const std::string &pluginId,
                               const TestCaseInfo &testInfo)
{
#ifdef _WIN32
    (void)settings;
    (void)kind;
    (void)path;
    (void)pluginId;
    return TestResult::crashed(testInfo.name, testInfo.description,
                               "Out-of-process execution is not supported on Windows.");
#else
    static std::atomic<uint64_t> counter{0};
    std::filesystem::path tmpDir = getValidatorTempDir();
    std::error_code ec;
    std::filesystem::create_directories(tmpDir, ec);
    std::filesystem::path outFile = tmpDir / ("result-" + std::to_string(counter++) + "-" +
                                              std::to_string(::getpid()) + ".txt");
    std::filesystem::remove(outFile, ec);

    // Build argv for: <self> run-single-test --output-file <file> <kind> <path> [<pluginId>] <name>
    std::vector<std::string> args;
    args.push_back(settings.executablePath);
    args.push_back("run-single-test");
    args.push_back("--output-file");
    args.push_back(outFile.string());
    if (settings.fullOutput)
    {
        args.push_back("--full-output");
    }
    args.push_back(kind == TestKind::Library ? "library" : "plugin");
    args.push_back(path.string());
    if (kind == TestKind::Plugin)
    {
        args.push_back(pluginId);
    }
    args.push_back(testInfo.name);

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &a : args)
    {
        argv.push_back(const_cast<char *>(a.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0)
    {
        return TestResult::crashed(testInfo.name, testInfo.description,
                                   "Could not fork a child process to run the check.");
    }
    if (pid == 0)
    {
        // Unless the plugin's own output was requested, silence its stdout/stderr chatter so it
        // doesn't intersperse with the validator's output. The child reports its result through the
        // output file, and a crash is reported by the parent via the exit signal, so nothing useful
        // is lost.
        if (settings.suppressPluginStdout)
        {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0)
            {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        execvp(settings.executablePath.c_str(), argv.data());
        _exit(127); // execvp only returns on failure
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return TestResult::crashed(testInfo.name, testInfo.description,
                                       "Could not wait for the check's child process.");
        }
    }

    if (WIFSIGNALED(status))
    {
        int sig = WTERMSIG(status);
        return TestResult::crashed(
            testInfo.name, testInfo.description,
            "The plugin crashed the validator: the check process was terminated by signal " +
                std::to_string(sig) + " (" + strsignal(sig) + ").");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return TestResult::crashed(testInfo.name, testInfo.description,
                                   "The check process exited abnormally with code " +
                                       std::to_string(code) + ".");
    }

    auto result = readResultFile(outFile, testInfo.description);
    std::filesystem::remove(outFile, ec);
    if (!result)
    {
        return TestResult::crashed(testInfo.name, testInfo.description,
                                   "The check process exited cleanly but did not write a result.");
    }
    return *result;
#endif
}
} // namespace

// Check if a test name matches the filter
bool matchesFilter(const std::string &testName, const ValidatorSettings &settings)
{
    if (!settings.testFilter)
    {
        return true;
    }

    try
    {
        std::regex filterRegex(*settings.testFilter, std::regex::icase);
        bool matches = std::regex_search(testName, filterRegex);
        return settings.invertFilter ? !matches : matches;
    }
    catch (const std::regex_error &)
    {
        // If regex is invalid, treat as literal substring match
        bool matches = testName.find(*settings.testFilter) != std::string::npos;
        return settings.invertFilter ? !matches : matches;
    }
}

void printTestResult(const TestResult &result, bool json, bool onlyFailed)
{
    if (onlyFailed && !result.isFailedOrWarning())
    {
        return;
    }

    if (json)
    {
        // JSON output handled elsewhere
        return;
    }

    // Color codes for terminal output
    const char *colorReset = "\033[0m";
    const char *colorGreen = "\033[32m";
    const char *colorRed = "\033[31m";
    const char *colorYellow = "\033[33m";
    const char *colorGray = "\033[90m";

    const char *statusColor;
    const char *statusText;

    switch (result.status)
    {
    case TestStatusCode::Success:
        statusColor = colorGreen;
        statusText = "PASS";
        break;
    case TestStatusCode::Failed:
        statusColor = colorRed;
        statusText = "FAIL";
        break;
    case TestStatusCode::Crashed:
        statusColor = colorRed;
        statusText = "CRASH";
        break;
    case TestStatusCode::Warning:
        statusColor = colorYellow;
        statusText = "WARN";
        break;
    case TestStatusCode::Skipped:
        statusColor = colorGray;
        statusText = "SKIP";
        break;
    }

    std::cout << "    [" << statusColor << statusText << colorReset << "] " << result.name;

    if (result.details)
    {
        // Indent the details, and every continuation line of a multi-line detail block, so the
        // output stays aligned instead of dumping a wall of text at column zero.
        const std::string indent = "           ";
        std::string rendered;
        for (char c : *result.details)
        {
            rendered += c;
            if (c == '\n')
            {
                rendered += indent;
            }
        }
        std::cout << "\n" << indent << rendered;
    }
    std::cout << "\n";
}

int validate(const ValidatorSettings &settings)
{
    if (settings.paths.empty())
    {
        std::cerr << "Error: No plugin paths specified\n";
        return 1;
    }

    uint32_t totalPassed = 0;
    uint32_t totalFailed = 0;
    uint32_t totalSkipped = 0;
    uint32_t totalWarnings = 0;

    auto libraryTests = PluginLibraryTests::getAllTests();
    auto pluginTests = PluginTests::getAllTests();

    // Controls detail truncation for checks run in this process (the --in-process path).
    PluginTests::setFullOutput(settings.fullOutput);

    if (settings.json)
    {
        std::cout << "{\n  \"results\": [\n";
    }

    bool firstResult = true;

    for (const auto &path : settings.paths)
    {
        if (!settings.json)
        {
            std::cout << "\nValidating: " << path.string() << "\n";
        }

        // Run plugin library tests
        if (!settings.json)
        {
            std::cout << "  Library tests:\n";
        }

        for (const auto &testInfo : libraryTests)
        {
            if (!matchesFilter(testInfo.name, settings))
            {
                continue;
            }
            if (testInfo.dangerous && !settings.dangerousTests)
            {
                continue;
            }

            TestResult result =
                settings.inProcess
                    ? PluginLibraryTests::runTest(testInfo.name, path)
                    : runTestOutOfProcess(settings, TestKind::Library, path, "", testInfo);

            switch (result.status)
            {
            case TestStatusCode::Success:
                totalPassed++;
                break;
            case TestStatusCode::Failed:
            case TestStatusCode::Crashed:
                totalFailed++;
                break;
            case TestStatusCode::Skipped:
                totalSkipped++;
                break;
            case TestStatusCode::Warning:
                totalWarnings++;
                break;
            }

            if (settings.json)
            {
                if (!firstResult)
                    std::cout << ",\n";
                firstResult = false;

                std::cout << "    {\n";
                std::cout << "      \"path\": \"" << path.string() << "\",\n";
                std::cout << "      \"test\": \"" << result.name << "\",\n";
                std::cout << "      \"status\": \"" << statusCodeToString(result.status) << "\"";
                if (result.details)
                {
                    std::cout << ",\n      \"details\": \"" << jsonEscape(*result.details) << "\"";
                }
                std::cout << "\n    }";
            }
            else
            {
                printTestResult(result, settings.json, settings.onlyFailed);
            }
        }

        // Load the library to run per-plugin tests
        try
        {
            std::unique_ptr<PluginLibrary> library;
            PluginLibraryMetadata metadata;
            {
                // The plugin often prints to stdout/stderr while its entry point initializes; hush
                // it so it doesn't intersperse with the validator's output.
                OutputSilencer silence(settings.suppressPluginStdout);
                library = PluginLibrary::load(path);
                metadata = library->metadata();
            }

            if (!isVersionCompatible(metadata.clapVersion()))
            {
                if (!settings.json)
                {
                    std::cout << "  Skipping: incompatible CLAP version\n";
                }
                continue;
            }

            for (const auto &pluginMeta : metadata.plugins)
            {
                // Filter by plugin ID if specified
                if (settings.pluginId && pluginMeta.id != *settings.pluginId)
                {
                    continue;
                }

                if (!settings.json)
                {
                    std::cout << "  Plugin: " << pluginMeta.name << " (" << pluginMeta.id << ")\n";
                }

                for (const auto &testInfo : pluginTests)
                {
                    if (!matchesFilter(testInfo.name, settings))
                    {
                        continue;
                    }
                    if (testInfo.dangerous && !settings.dangerousTests)
                    {
                        continue;
                    }

                    TestResult result =
                        settings.inProcess
                            ? PluginTests::runTest(testInfo.name, *library, pluginMeta.id)
                            : runTestOutOfProcess(settings, TestKind::Plugin, path, pluginMeta.id,
                                                  testInfo);

                    switch (result.status)
                    {
                    case TestStatusCode::Success:
                        totalPassed++;
                        break;
                    case TestStatusCode::Failed:
                    case TestStatusCode::Crashed:
                        totalFailed++;
                        break;
                    case TestStatusCode::Skipped:
                        totalSkipped++;
                        break;
                    case TestStatusCode::Warning:
                        totalWarnings++;
                        break;
                    }

                    if (settings.json)
                    {
                        if (!firstResult)
                            std::cout << ",\n";
                        firstResult = false;

                        std::cout << "    {\n";
                        std::cout << "      \"path\": \"" << path.string() << "\",\n";
                        std::cout << "      \"plugin_id\": \"" << pluginMeta.id << "\",\n";
                        std::cout << "      \"test\": \"" << result.name << "\",\n";
                        std::cout << "      \"status\": \"" << statusCodeToString(result.status)
                                  << "\"";
                        if (result.details)
                        {
                            std::cout << ",\n      \"details\": \"" << jsonEscape(*result.details)
                                      << "\"";
                        }
                        std::cout << "\n    }";
                    }
                    else
                    {
                        printTestResult(result, settings.json, settings.onlyFailed);
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            if (!settings.json)
            {
                std::cerr << "  Error loading library: " << e.what() << "\n";
            }
            totalFailed++;
        }
    }

    if (settings.json)
    {
        std::cout << "\n  ],\n";
        std::cout << "  \"summary\": {\n";
        std::cout << "    \"passed\": " << totalPassed << ",\n";
        std::cout << "    \"failed\": " << totalFailed << ",\n";
        std::cout << "    \"skipped\": " << totalSkipped << ",\n";
        std::cout << "    \"warnings\": " << totalWarnings << "\n";
        std::cout << "  }\n}\n";
    }
    else
    {
        std::cout << "\n";
        std::cout << "Summary:\n";
        std::cout << "  Passed:   " << totalPassed << "\n";
        std::cout << "  Failed:   " << totalFailed << "\n";
        std::cout << "  Skipped:  " << totalSkipped << "\n";
        std::cout << "  Warnings: " << totalWarnings << "\n";
    }

    return totalFailed > 0 ? 1 : 0;
}

int runSingleTest(const SingleTestSettings &settings)
{
    PluginTests::setFullOutput(settings.fullOutput);

    TestResult result = TestResult::failed(settings.testName, "", "not run");
    try
    {
        if (settings.kind == TestKind::Library)
        {
            result = PluginLibraryTests::runTest(settings.testName, settings.path);
        }
        else
        {
            auto library = PluginLibrary::load(settings.path);
            result = PluginTests::runTest(settings.testName, *library, settings.pluginId);
        }
    }
    catch (const std::exception &e)
    {
        result = TestResult::failed(settings.testName, "", e.what());
    }

    writeResultFile(settings.outputFile, result);
    return 0;
}

} // namespace commands
} // namespace clap_validator
