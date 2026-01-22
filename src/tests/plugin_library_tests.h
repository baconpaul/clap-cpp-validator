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

#ifndef CLAPVALCPP_SRC_TESTS_PLUGIN_LIBRARY_TESTS_H
#define CLAPVALCPP_SRC_TESTS_PLUGIN_LIBRARY_TESTS_H

#include "test_case.h"
#include <filesystem>
#include <vector>

namespace clap_validator
{

// Tests for entire plugin libraries (scanning behavior, factory queries, etc.)
class PluginLibraryTests
{
  public:
    // Get all available plugin library test cases
    static std::vector<TestCaseInfo> getAllTests();

    // Run a specific test by name
    static TestResult runTest(const std::string &testName,
                              const std::filesystem::path &libraryPath);

    // Individual test implementations
    static TestResult testScanTime(const std::filesystem::path &libraryPath);
    static TestResult testQueryNonexistentFactory(const std::filesystem::path &libraryPath);
    static TestResult testCreateIdWithTrailingGarbage(const std::filesystem::path &libraryPath);

  private:
    static constexpr int SCAN_TIME_LIMIT_MS = 100;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_TESTS_PLUGIN_LIBRARY_TESTS_H
