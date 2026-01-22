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
