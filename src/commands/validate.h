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

#ifndef CLAPVALCPP_SRC_COMMANDS_VALIDATE_H
#define CLAPVALCPP_SRC_COMMANDS_VALIDATE_H

#include <vector>
#include <string>
#include <optional>
#include <filesystem>

namespace clap_validator
{

// Settings for the validator
struct ValidatorSettings
{
    std::vector<std::filesystem::path> paths;
    std::optional<std::string> pluginId;
    std::optional<std::string> testFilter;
    bool invertFilter = false;
    bool json = false;
    bool onlyFailed = false;
    // When false (the default) each check runs in a child process so a crashing plugin is reported
    // as Crashed instead of taking down the validator. On Windows this always runs in-process.
    bool inProcess = false;
    // When true, show untruncated detail lists (e.g. every mismatching parameter).
    bool fullOutput = false;
    // When true (the default) the plugin's own stdout/stderr is hushed so it doesn't intersperse
    // with the validator's output.
    bool suppressPluginStdout = true;
    // Path to this executable, used to re-spawn it for out-of-process checks.
    std::string executablePath;
};

// Whether a check operates on the whole library or a single plugin instance.
enum class TestKind
{
    Library,
    Plugin
};

// Settings for the hidden `run-single-test` subcommand, which runs one check in-process and writes
// its result to a file. This is what the out-of-process runner spawns.
struct SingleTestSettings
{
    TestKind kind = TestKind::Library;
    std::filesystem::path path;
    std::string pluginId; // only used for TestKind::Plugin
    std::string testName;
    std::filesystem::path outputFile;
    bool fullOutput = false;
};

namespace commands
{

// Run validation on the specified plugins
int validate(const ValidatorSettings &settings);

// Run a single check in-process and write its result to settings.outputFile. Returns 0 on success.
int runSingleTest(const SingleTestSettings &settings);

} // namespace commands
} // namespace clap_validator

#endif // CLAPVALCPP_SRC_COMMANDS_VALIDATE_H
