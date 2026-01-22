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

#ifndef CLAPVALCPP_SRC_TESTS_TEST_CASE_H
#define CLAPVALCPP_SRC_TESTS_TEST_CASE_H

#include <string>
#include <optional>
#include <vector>
#include <functional>

namespace clap_validator
{

// The result of running a test
enum class TestStatusCode
{
    Success,
    Crashed,
    Failed,
    Skipped,
    Warning
};

// The result of running a test case
struct TestResult
{
    std::string name;
    std::string description;
    TestStatusCode status;
    std::optional<std::string> details;

    static TestResult success(const std::string &name, const std::string &description,
                              const std::optional<std::string> &details = std::nullopt)
    {
        return {name, description, TestStatusCode::Success, details};
    }

    static TestResult failed(const std::string &name, const std::string &description,
                             const std::optional<std::string> &details = std::nullopt)
    {
        return {name, description, TestStatusCode::Failed, details};
    }

    static TestResult skipped(const std::string &name, const std::string &description,
                              const std::optional<std::string> &details = std::nullopt)
    {
        return {name, description, TestStatusCode::Skipped, details};
    }

    static TestResult warning(const std::string &name, const std::string &description,
                              const std::optional<std::string> &details = std::nullopt)
    {
        return {name, description, TestStatusCode::Warning, details};
    }

    static TestResult crashed(const std::string &name, const std::string &description,
                              const std::string &details)
    {
        return {name, description, TestStatusCode::Crashed, details};
    }

    bool isFailedOrWarning() const
    {
        return status == TestStatusCode::Failed || status == TestStatusCode::Crashed ||
               status == TestStatusCode::Warning;
    }
};

// Information about a test case
struct TestCaseInfo
{
    std::string name;
    std::string description;
};

// Get the status code as a string
std::string statusCodeToString(TestStatusCode status);

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_TESTS_TEST_CASE_H
