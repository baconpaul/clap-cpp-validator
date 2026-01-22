#include "plugin_library_tests.h"
#include "../plugin/library.h"
#include "../plugin/host.h"
#include "../plugin/instance.h"
#include <chrono>

namespace clap_validator {

std::vector<TestCaseInfo> PluginLibraryTests::getAllTests() {
    return {
        {"scan-time", 
         "Checks whether the plugin can be scanned in under " + std::to_string(SCAN_TIME_LIMIT_MS) + " milliseconds."},
        {"query-factory-nonexistent",
         "Tries to query a factory from the plugin's entry point with a non-existent ID. This should return a null pointer."},
        {"create-id-with-trailing-garbage",
         "Attempts to create a plugin instance using an existing plugin ID with some extra text appended to the end. This should return a null pointer."}
    };
}

TestResult PluginLibraryTests::runTest(const std::string& testName, const std::filesystem::path& libraryPath) {
    if (testName == "scan-time") {
        return testScanTime(libraryPath);
    } else if (testName == "query-factory-nonexistent") {
        return testQueryNonexistentFactory(libraryPath);
    } else if (testName == "create-id-with-trailing-garbage") {
        return testCreateIdWithTrailingGarbage(libraryPath);
    }
    
    return TestResult::failed(testName, "Unknown test", "Test '" + testName + "' not found");
}

TestResult PluginLibraryTests::testScanTime(const std::filesystem::path& libraryPath) {
    const std::string testName = "scan-time";
    const std::string description = "Checks whether the plugin can be scanned in under " + 
                                    std::to_string(SCAN_TIME_LIMIT_MS) + " milliseconds.";
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto library = PluginLibrary::load(libraryPath);
        auto metadata = library->metadata();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (duration.count() > SCAN_TIME_LIMIT_MS) {
            return TestResult::warning(testName, description,
                "Plugin took " + std::to_string(duration.count()) + "ms to scan (limit: " + 
                std::to_string(SCAN_TIME_LIMIT_MS) + "ms)");
        }
        
        return TestResult::success(testName, description,
            "Plugin scanned in " + std::to_string(duration.count()) + "ms");
            
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginLibraryTests::testQueryNonexistentFactory(const std::filesystem::path& libraryPath) {
    const std::string testName = "query-factory-nonexistent";
    const std::string description = "Tries to query a factory from the plugin's entry point with a non-existent ID.";
    
    try {
        auto library = PluginLibrary::load(libraryPath);
        
        // Query with a non-existent factory ID
        bool exists = library->factoryExists("com.nonexistent.factory.that.should.not.exist");
        
        if (exists) {
            return TestResult::failed(testName, description,
                "Plugin returned a non-null pointer for a non-existent factory ID");
        }
        
        return TestResult::success(testName, description);
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginLibraryTests::testCreateIdWithTrailingGarbage(const std::filesystem::path& libraryPath) {
    const std::string testName = "create-id-with-trailing-garbage";
    const std::string description = "Attempts to create a plugin instance using an existing plugin ID with trailing garbage.";
    
    try {
        auto library = PluginLibrary::load(libraryPath);
        auto metadata = library->metadata();
        
        if (metadata.plugins.empty()) {
            return TestResult::skipped(testName, description, "No plugins found in library");
        }
        
        // Get the first plugin's ID and add garbage to it
        std::string validId = metadata.plugins[0].id;
        std::string invalidId = validId + "_GARBAGE_THAT_SHOULD_NOT_MATCH";
        
        auto host = std::make_shared<Host>();
        
        try {
            auto plugin = library->createPlugin(invalidId, host);
            // If we get here, the plugin was created when it shouldn't have been
            return TestResult::failed(testName, description,
                "Plugin was created with invalid ID '" + invalidId + "' (should have returned null)");
        } catch (const std::exception&) {
            // Expected - plugin creation should fail
            return TestResult::success(testName, description,
                "Plugin correctly rejected ID with trailing garbage");
        }
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

} // namespace clap_validator
