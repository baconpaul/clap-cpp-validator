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

#ifndef CLAPVALCPP_SRC_TESTS_PLUGIN_TESTS_H
#define CLAPVALCPP_SRC_TESTS_PLUGIN_TESTS_H

#include "test_case.h"
#include <vector>
#include <string>
#include <memory>

namespace clap_validator
{

class PluginLibrary;

// Tests for individual plugin instances
class PluginTests
{
  public:
    // Get all available plugin test cases
    static std::vector<TestCaseInfo> getAllTests();

    // Run a specific test by name
    static TestResult runTest(const std::string &testName, PluginLibrary &library,
                              const std::string &pluginId);

    // When enabled, detail lists (e.g. parameter mismatches) are shown in full rather than
    // truncated. Set before running checks.
    static void setFullOutput(bool full);

    // Descriptor tests
    static TestResult testDescriptorConsistency(PluginLibrary &library,
                                                const std::string &pluginId);
    static TestResult testFeaturesCategories(PluginLibrary &library, const std::string &pluginId);
    static TestResult testFeaturesDuplicates(PluginLibrary &library, const std::string &pluginId);

    // Processing tests
    static TestResult testProcessAudioOutOfPlaceBasic(PluginLibrary &library,
                                                      const std::string &pluginId);
    static TestResult testProcessNoteOutOfPlaceBasic(PluginLibrary &library,
                                                     const std::string &pluginId);
    static TestResult testProcessNoteInconsistent(PluginLibrary &library,
                                                  const std::string &pluginId);
    static TestResult testProcessReactivation(PluginLibrary &library, const std::string &pluginId);

    // Parameter tests
    static TestResult testParamConversions(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamFuzzBasic(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamSetWrongNamespace(PluginLibrary &library,
                                                 const std::string &pluginId);

    // State tests
    static TestResult testStateInvalid(PluginLibrary &library, const std::string &pluginId);
    static TestResult testStateReproducibilityBasic(PluginLibrary &library,
                                                    const std::string &pluginId);
    static TestResult testStateReproducibilityNullCookies(PluginLibrary &library,
                                                          const std::string &pluginId);
    static TestResult testStateReproducibilityFlush(PluginLibrary &library,
                                                    const std::string &pluginId);
    static TestResult testStateBufferedStreams(PluginLibrary &library, const std::string &pluginId);

    // Other extension tests (not present in the Rust validator)
    static TestResult testContextMenu(PluginLibrary &library, const std::string &pluginId);
    static TestResult testLatency(PluginLibrary &library, const std::string &pluginId);
    static TestResult testTail(PluginLibrary &library, const std::string &pluginId);
    static TestResult testVoiceInfo(PluginLibrary &library, const std::string &pluginId);
    static TestResult testNoteName(PluginLibrary &library, const std::string &pluginId);
    static TestResult testRender(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamDefaults(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamInfoStable(PluginLibrary &library, const std::string &pluginId);
    static TestResult testAudioPortsConfig(PluginLibrary &library, const std::string &pluginId);
    static TestResult testRemoteControls(PluginLibrary &library, const std::string &pluginId);
    static TestResult testStateContext(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamIndication(PluginLibrary &library, const std::string &pluginId);
    static TestResult testGetExtensionContract(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamRangeRobustness(PluginLibrary &library, const std::string &pluginId);
    static TestResult testLifecycleNegativePath(PluginLibrary &library,
                                                const std::string &pluginId);

  private:
    // Helper for state reproducibility tests with optional null cookies
    static TestResult testStateReproducibilityImpl(PluginLibrary &library,
                                                   const std::string &pluginId,
                                                   bool zeroOutCookies);

    // Constants for param fuzzing
    static constexpr size_t FUZZ_NUM_PERMUTATIONS = 50;
    static constexpr size_t FUZZ_RUNS_PER_PERMUTATION = 5;
    static constexpr size_t BUFFER_SIZE = 512;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_TESTS_PLUGIN_TESTS_H
