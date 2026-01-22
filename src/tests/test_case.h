#pragma once

#include <string>
#include <optional>
#include <vector>
#include <functional>

namespace clap_validator {

// The result of running a test
enum class TestStatusCode {
    Success,
    Crashed,
    Failed,
    Skipped,
    Warning
};

// The result of running a test case
struct TestResult {
    std::string name;
    std::string description;
    TestStatusCode status;
    std::optional<std::string> details;
    
    static TestResult success(const std::string& name, const std::string& description,
                              const std::optional<std::string>& details = std::nullopt) {
        return {name, description, TestStatusCode::Success, details};
    }
    
    static TestResult failed(const std::string& name, const std::string& description,
                             const std::optional<std::string>& details = std::nullopt) {
        return {name, description, TestStatusCode::Failed, details};
    }
    
    static TestResult skipped(const std::string& name, const std::string& description,
                              const std::optional<std::string>& details = std::nullopt) {
        return {name, description, TestStatusCode::Skipped, details};
    }
    
    static TestResult warning(const std::string& name, const std::string& description,
                              const std::optional<std::string>& details = std::nullopt) {
        return {name, description, TestStatusCode::Warning, details};
    }
    
    static TestResult crashed(const std::string& name, const std::string& description,
                              const std::string& details) {
        return {name, description, TestStatusCode::Crashed, details};
    }
    
    bool isFailedOrWarning() const {
        return status == TestStatusCode::Failed || 
               status == TestStatusCode::Crashed || 
               status == TestStatusCode::Warning;
    }
};

// Information about a test case
struct TestCaseInfo {
    std::string name;
    std::string description;
};

// Get the status code as a string
std::string statusCodeToString(TestStatusCode status);

} // namespace clap_validator
