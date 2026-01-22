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
#include "validator.h"

namespace clap_validator
{

ValidationTally computeTally(const ValidationResult &result)
{
    ValidationTally tally;

    // Count plugin library test results
    for (const auto &[path, tests] : result.pluginLibraryTests)
    {
        for (const auto &test : tests)
        {
            switch (test.status)
            {
            case TestStatusCode::Success:
                tally.numPassed++;
                break;
            case TestStatusCode::Failed:
            case TestStatusCode::Crashed:
                tally.numFailed++;
                break;
            case TestStatusCode::Skipped:
                tally.numSkipped++;
                break;
            case TestStatusCode::Warning:
                tally.numWarnings++;
                break;
            }
        }
    }

    // Count plugin test results
    for (const auto &[pluginId, tests] : result.pluginTests)
    {
        for (const auto &test : tests)
        {
            switch (test.status)
            {
            case TestStatusCode::Success:
                tally.numPassed++;
                break;
            case TestStatusCode::Failed:
            case TestStatusCode::Crashed:
                tally.numFailed++;
                break;
            case TestStatusCode::Skipped:
                tally.numSkipped++;
                break;
            case TestStatusCode::Warning:
                tally.numWarnings++;
                break;
            }
        }
    }

    return tally;
}

} // namespace clap_validator
