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
#include "test_case.h"

namespace clap_validator
{

std::string statusCodeToString(TestStatusCode status)
{
    switch (status)
    {
    case TestStatusCode::Success:
        return "success";
    case TestStatusCode::Crashed:
        return "crashed";
    case TestStatusCode::Failed:
        return "failed";
    case TestStatusCode::Skipped:
        return "skipped";
    case TestStatusCode::Warning:
        return "warning";
    default:
        return "unknown";
    }
}

} // namespace clap_validator
