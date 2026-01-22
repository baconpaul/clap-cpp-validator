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

    // Individual test implementations
    static TestResult testDescriptorConsistency(PluginLibrary &library,
                                                const std::string &pluginId);
    static TestResult testFeaturesCategories(PluginLibrary &library, const std::string &pluginId);
    static TestResult testFeaturesDuplicates(PluginLibrary &library, const std::string &pluginId);
    static TestResult testProcessAudioBasic(PluginLibrary &library, const std::string &pluginId);
    static TestResult testParamConversions(PluginLibrary &library, const std::string &pluginId);
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_TESTS_PLUGIN_TESTS_H
