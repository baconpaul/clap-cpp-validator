#pragma once

#include "test_case.h"
#include <filesystem>
#include <vector>

namespace clap_validator {

// Tests for entire plugin libraries (scanning behavior, factory queries, etc.)
class PluginLibraryTests {
public:
    // Get all available plugin library test cases
    static std::vector<TestCaseInfo> getAllTests();
    
    // Run a specific test by name
    static TestResult runTest(const std::string& testName, const std::filesystem::path& libraryPath);
    
    // Individual test implementations
    static TestResult testScanTime(const std::filesystem::path& libraryPath);
    static TestResult testQueryNonexistentFactory(const std::filesystem::path& libraryPath);
    static TestResult testCreateIdWithTrailingGarbage(const std::filesystem::path& libraryPath);
    
private:
    static constexpr int SCAN_TIME_LIMIT_MS = 100;
};

} // namespace clap_validator
