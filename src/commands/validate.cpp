#include "validate.h"
#include "../plugin/library.h"
#include "../tests/plugin_library_tests.h"
#include "../tests/plugin_tests.h"
#include "../util.h"
#include <iostream>
#include <regex>

namespace clap_validator {
namespace commands {

// Check if a test name matches the filter
bool matchesFilter(const std::string& testName, const ValidatorSettings& settings) {
    if (!settings.testFilter) {
        return true;
    }
    
    try {
        std::regex filterRegex(*settings.testFilter, std::regex::icase);
        bool matches = std::regex_search(testName, filterRegex);
        return settings.invertFilter ? !matches : matches;
    } catch (const std::regex_error&) {
        // If regex is invalid, treat as literal substring match
        bool matches = testName.find(*settings.testFilter) != std::string::npos;
        return settings.invertFilter ? !matches : matches;
    }
}

void printTestResult(const TestResult& result, bool json, bool onlyFailed) {
    if (onlyFailed && !result.isFailedOrWarning()) {
        return;
    }
    
    if (json) {
        // JSON output handled elsewhere
        return;
    }
    
    // Color codes for terminal output
    const char* colorReset = "\033[0m";
    const char* colorGreen = "\033[32m";
    const char* colorRed = "\033[31m";
    const char* colorYellow = "\033[33m";
    const char* colorGray = "\033[90m";
    
    const char* statusColor;
    const char* statusText;
    
    switch (result.status) {
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
    
    if (result.details) {
        std::cout << "\n           " << *result.details;
    }
    std::cout << "\n";
}

int validate(const ValidatorSettings& settings) {
    if (settings.paths.empty()) {
        std::cerr << "Error: No plugin paths specified\n";
        return 1;
    }
    
    uint32_t totalPassed = 0;
    uint32_t totalFailed = 0;
    uint32_t totalSkipped = 0;
    uint32_t totalWarnings = 0;
    
    auto libraryTests = PluginLibraryTests::getAllTests();
    auto pluginTests = PluginTests::getAllTests();
    
    if (settings.json) {
        std::cout << "{\n  \"results\": [\n";
    }
    
    bool firstResult = true;
    
    for (const auto& path : settings.paths) {
        if (!settings.json) {
            std::cout << "\nValidating: " << path.string() << "\n";
        }
        
        // Run plugin library tests
        if (!settings.json) {
            std::cout << "  Library tests:\n";
        }
        
        for (const auto& testInfo : libraryTests) {
            if (!matchesFilter(testInfo.name, settings)) {
                continue;
            }
            
            TestResult result = PluginLibraryTests::runTest(testInfo.name, path);
            
            switch (result.status) {
                case TestStatusCode::Success: totalPassed++; break;
                case TestStatusCode::Failed:
                case TestStatusCode::Crashed: totalFailed++; break;
                case TestStatusCode::Skipped: totalSkipped++; break;
                case TestStatusCode::Warning: totalWarnings++; break;
            }
            
            if (settings.json) {
                if (!firstResult) std::cout << ",\n";
                firstResult = false;
                
                std::cout << "    {\n";
                std::cout << "      \"path\": \"" << path.string() << "\",\n";
                std::cout << "      \"test\": \"" << result.name << "\",\n";
                std::cout << "      \"status\": \"" << statusCodeToString(result.status) << "\"";
                if (result.details) {
                    std::cout << ",\n      \"details\": \"" << *result.details << "\"";
                }
                std::cout << "\n    }";
            } else {
                printTestResult(result, settings.json, settings.onlyFailed);
            }
        }
        
        // Load the library to run per-plugin tests
        try {
            auto library = PluginLibrary::load(path);
            auto metadata = library->metadata();
            
            if (!isVersionCompatible(metadata.clapVersion())) {
                if (!settings.json) {
                    std::cout << "  Skipping: incompatible CLAP version\n";
                }
                continue;
            }
            
            for (const auto& pluginMeta : metadata.plugins) {
                // Filter by plugin ID if specified
                if (settings.pluginId && pluginMeta.id != *settings.pluginId) {
                    continue;
                }
                
                if (!settings.json) {
                    std::cout << "  Plugin: " << pluginMeta.name << " (" << pluginMeta.id << ")\n";
                }
                
                for (const auto& testInfo : pluginTests) {
                    if (!matchesFilter(testInfo.name, settings)) {
                        continue;
                    }
                    
                    TestResult result = PluginTests::runTest(testInfo.name, *library, pluginMeta.id);
                    
                    switch (result.status) {
                        case TestStatusCode::Success: totalPassed++; break;
                        case TestStatusCode::Failed:
                        case TestStatusCode::Crashed: totalFailed++; break;
                        case TestStatusCode::Skipped: totalSkipped++; break;
                        case TestStatusCode::Warning: totalWarnings++; break;
                    }
                    
                    if (settings.json) {
                        if (!firstResult) std::cout << ",\n";
                        firstResult = false;
                        
                        std::cout << "    {\n";
                        std::cout << "      \"path\": \"" << path.string() << "\",\n";
                        std::cout << "      \"plugin_id\": \"" << pluginMeta.id << "\",\n";
                        std::cout << "      \"test\": \"" << result.name << "\",\n";
                        std::cout << "      \"status\": \"" << statusCodeToString(result.status) << "\"";
                        if (result.details) {
                            std::cout << ",\n      \"details\": \"" << *result.details << "\"";
                        }
                        std::cout << "\n    }";
                    } else {
                        printTestResult(result, settings.json, settings.onlyFailed);
                    }
                }
            }
        } catch (const std::exception& e) {
            if (!settings.json) {
                std::cerr << "  Error loading library: " << e.what() << "\n";
            }
            totalFailed++;
        }
    }
    
    if (settings.json) {
        std::cout << "\n  ],\n";
        std::cout << "  \"summary\": {\n";
        std::cout << "    \"passed\": " << totalPassed << ",\n";
        std::cout << "    \"failed\": " << totalFailed << ",\n";
        std::cout << "    \"skipped\": " << totalSkipped << ",\n";
        std::cout << "    \"warnings\": " << totalWarnings << "\n";
        std::cout << "  }\n}\n";
    } else {
        std::cout << "\n";
        std::cout << "Summary:\n";
        std::cout << "  Passed:   " << totalPassed << "\n";
        std::cout << "  Failed:   " << totalFailed << "\n";
        std::cout << "  Skipped:  " << totalSkipped << "\n";
        std::cout << "  Warnings: " << totalWarnings << "\n";
    }
    
    return totalFailed > 0 ? 1 : 0;
}

} // namespace commands
} // namespace clap_validator
