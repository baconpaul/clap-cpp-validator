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
