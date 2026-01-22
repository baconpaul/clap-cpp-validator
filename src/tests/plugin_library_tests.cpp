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
#include "plugin_library_tests.h"
#include "../plugin/library.h"
#include "../plugin/host.h"
#include "../plugin/instance.h"
#include <chrono>
#include <random>

#ifdef __unix__
#include <dlfcn.h>
#endif

namespace clap_validator
{

std::vector<TestCaseInfo> PluginLibraryTests::getAllTests()
{
    return {
        {"scan-time", "Checks whether the plugin can be scanned in under " +
                          std::to_string(SCAN_TIME_LIMIT_MS) + " milliseconds."},
        {"scan-rtld-now",
         "Checks whether the plugin loads correctly when loaded using 'dlopen(..., RTLD_LOCAL | "
         "RTLD_NOW)'. Only run on Unix-like platforms."},
        {"query-factory-nonexistent", "Tries to query a factory from the plugin's entry point with "
                                      "a non-existent ID. This should return a null pointer."},
        {"create-id-with-trailing-garbage",
         "Attempts to create a plugin instance using an existing plugin ID with some extra text "
         "appended to the end. This should return a null pointer."},
        {"preset-discovery-crawl",
         "If the plugin supports the preset discovery mechanism, then this test ensures that all "
         "of the plugin's declared locations can be indexed successfully."},
        {"preset-discovery-descriptor-consistency",
         "Ensures that all preset provider descriptors from a preset discovery factory match those "
         "stored in the providers created by the factory."},
        {"preset-discovery-load",
         "The same as 'preset-discovery-crawl', but also tries to load all found presets for "
         "plugins supported by the CLAP plugin library."}};
}

TestResult PluginLibraryTests::runTest(const std::string &testName,
                                       const std::filesystem::path &libraryPath)
{
    if (testName == "scan-time")
    {
        return testScanTime(libraryPath);
    }
    else if (testName == "scan-rtld-now")
    {
        return testScanRtldNow(libraryPath);
    }
    else if (testName == "query-factory-nonexistent")
    {
        return testQueryNonexistentFactory(libraryPath);
    }
    else if (testName == "create-id-with-trailing-garbage")
    {
        return testCreateIdWithTrailingGarbage(libraryPath);
    }
    else if (testName == "preset-discovery-crawl")
    {
        return testPresetDiscoveryCrawl(libraryPath);
    }
    else if (testName == "preset-discovery-descriptor-consistency")
    {
        return testPresetDiscoveryDescriptorConsistency(libraryPath);
    }
    else if (testName == "preset-discovery-load")
    {
        return testPresetDiscoveryLoad(libraryPath);
    }

    return TestResult::failed(testName, "Unknown test", "Test '" + testName + "' not found");
}

TestResult PluginLibraryTests::testScanTime(const std::filesystem::path &libraryPath)
{
    const std::string testName = "scan-time";
    const std::string description = "Checks whether the plugin can be scanned in under " +
                                    std::to_string(SCAN_TIME_LIMIT_MS) + " milliseconds.";

    try
    {
        auto start = std::chrono::high_resolution_clock::now();

        auto library = PluginLibrary::load(libraryPath);
        auto metadata = library->metadata();

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (duration.count() > SCAN_TIME_LIMIT_MS)
        {
            return TestResult::warning(
                testName, description,
                "Plugin took " + std::to_string(duration.count()) +
                    "ms to scan (limit: " + std::to_string(SCAN_TIME_LIMIT_MS) + "ms)");
        }

        return TestResult::success(testName, description,
                                   "Plugin scanned in " + std::to_string(duration.count()) + "ms");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginLibraryTests::testQueryNonexistentFactory(const std::filesystem::path &libraryPath)
{
    const std::string testName = "query-factory-nonexistent";
    const std::string description =
        "Tries to query a factory from the plugin's entry point with a non-existent ID.";

    try
    {
        auto library = PluginLibrary::load(libraryPath);

        // Query with a non-existent factory ID
        bool exists = library->factoryExists("com.nonexistent.factory.that.should.not.exist");

        if (exists)
        {
            return TestResult::failed(
                testName, description,
                "Plugin returned a non-null pointer for a non-existent factory ID");
        }

        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult
PluginLibraryTests::testCreateIdWithTrailingGarbage(const std::filesystem::path &libraryPath)
{
    const std::string testName = "create-id-with-trailing-garbage";
    const std::string description =
        "Attempts to create a plugin instance using an existing plugin ID with trailing garbage.";

    try
    {
        auto library = PluginLibrary::load(libraryPath);
        auto metadata = library->metadata();

        if (metadata.plugins.empty())
        {
            return TestResult::skipped(testName, description, "No plugins found in library");
        }

        // Get the first plugin's ID and add garbage to it
        std::string validId = metadata.plugins[0].id;
        std::string invalidId = validId + "_GARBAGE_THAT_SHOULD_NOT_MATCH";

        auto host = std::make_shared<Host>();

        try
        {
            auto plugin = library->createPlugin(invalidId, host);
            // If we get here, the plugin was created when it shouldn't have been
            return TestResult::failed(testName, description,
                                      "Plugin was created with invalid ID '" + invalidId +
                                          "' (should have returned null)");
        }
        catch (const std::exception &)
        {
            // Expected - plugin creation should fail
            return TestResult::success(testName, description,
                                       "Plugin correctly rejected ID with trailing garbage");
        }
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginLibraryTests::testScanRtldNow(const std::filesystem::path &libraryPath)
{
    const std::string testName = "scan-rtld-now";
    const std::string description =
        "Checks whether the plugin loads correctly using 'dlopen(..., RTLD_LOCAL | RTLD_NOW)'.";

#ifdef __unix__
    try
    {
        // Try to load the library with RTLD_NOW to catch any unresolved symbols
        void *handle = dlopen(libraryPath.c_str(), RTLD_LOCAL | RTLD_NOW);
        if (!handle)
        {
            const char *error = dlerror();
            return TestResult::failed(testName, description,
                                      std::string("Failed to load with RTLD_NOW: ") +
                                          (error ? error : "unknown error"));
        }

        // Successfully loaded, now close it
        dlclose(handle);

        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
#else
    return TestResult::skipped(testName, description,
                               "This test is only relevant to Unix-like platforms");
#endif
}

TestResult PluginLibraryTests::testPresetDiscoveryCrawl(const std::filesystem::path &libraryPath)
{
    const std::string testName = "preset-discovery-crawl";
    const std::string description =
        "Ensures that all of the plugin's declared preset locations can be indexed successfully.";

    try
    {
        auto library = PluginLibrary::load(libraryPath);

        // Check if the preset discovery factory exists
        if (!library->factoryExists(CLAP_PRESET_DISCOVERY_FACTORY_ID))
        {
            return TestResult::skipped(
                testName, description,
                "The plugin does not implement the '" +
                    std::string(CLAP_PRESET_DISCOVERY_FACTORY_ID) + "' factory.");
        }

        // TODO: Implement full preset discovery crawling
        // For now, we just check if the factory exists
        return TestResult::skipped(testName, description,
                                   "Preset discovery crawling not yet fully implemented");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginLibraryTests::testPresetDiscoveryDescriptorConsistency(
    const std::filesystem::path &libraryPath)
{
    const std::string testName = "preset-discovery-descriptor-consistency";
    const std::string description =
        "Ensures that preset provider descriptors from the factory match those in the providers.";

    try
    {
        auto library = PluginLibrary::load(libraryPath);

        // Check if the preset discovery factory exists
        if (!library->factoryExists(CLAP_PRESET_DISCOVERY_FACTORY_ID))
        {
            return TestResult::skipped(
                testName, description,
                "The plugin does not implement the '" +
                    std::string(CLAP_PRESET_DISCOVERY_FACTORY_ID) + "' factory.");
        }

        // TODO: Implement full preset discovery descriptor consistency check
        return TestResult::skipped(
            testName, description,
            "Preset discovery descriptor consistency check not yet fully implemented");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginLibraryTests::testPresetDiscoveryLoad(const std::filesystem::path &libraryPath)
{
    const std::string testName = "preset-discovery-load";
    const std::string description =
        "Crawls preset locations and tries to load all found presets.";

    try
    {
        auto library = PluginLibrary::load(libraryPath);

        // Check if the preset discovery factory exists
        if (!library->factoryExists(CLAP_PRESET_DISCOVERY_FACTORY_ID))
        {
            return TestResult::skipped(
                testName, description,
                "The plugin does not implement the '" +
                    std::string(CLAP_PRESET_DISCOVERY_FACTORY_ID) + "' factory.");
        }

        // TODO: Implement full preset discovery and loading
        return TestResult::skipped(testName, description,
                                   "Preset discovery loading not yet fully implemented");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

} // namespace clap_validator
