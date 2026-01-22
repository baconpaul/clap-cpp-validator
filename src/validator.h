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

#ifndef CLAPVALCPP_SRC_VALIDATOR_H
#define CLAPVALCPP_SRC_VALIDATOR_H

#include "commands/validate.h"
#include "tests/test_case.h"
#include <map>
#include <vector>
#include <filesystem>

namespace clap_validator
{

// Results of running the validation test suite
struct ValidationResult
{
    // Results indexed by plugin library path
    std::map<std::filesystem::path, std::vector<TestResult>> pluginLibraryTests;
    // Results indexed by plugin ID
    std::map<std::string, std::vector<TestResult>> pluginTests;
};

// Statistics for the validator
struct ValidationTally
{
    uint32_t numPassed = 0;
    uint32_t numFailed = 0;
    uint32_t numSkipped = 0;
    uint32_t numWarnings = 0;

    uint32_t total() const { return numPassed + numFailed + numSkipped + numWarnings; }
};

// Compute tally from validation results
ValidationTally computeTally(const ValidationResult &result);

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_VALIDATOR_H
