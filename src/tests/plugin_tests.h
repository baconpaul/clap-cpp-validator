#pragma once

#include "test_case.h"
#include <vector>
#include <string>
#include <memory>

namespace clap_validator {

class PluginLibrary;

// Tests for individual plugin instances
class PluginTests {
public:
    // Get all available plugin test cases
    static std::vector<TestCaseInfo> getAllTests();
    
    // Run a specific test by name
    static TestResult runTest(const std::string& testName, 
                              PluginLibrary& library, 
                              const std::string& pluginId);
    
    // Individual test implementations
    static TestResult testDescriptorConsistency(PluginLibrary& library, const std::string& pluginId);
    static TestResult testFeaturesCategories(PluginLibrary& library, const std::string& pluginId);
    static TestResult testFeaturesDuplicates(PluginLibrary& library, const std::string& pluginId);
    static TestResult testProcessAudioBasic(PluginLibrary& library, const std::string& pluginId);
    static TestResult testParamConversions(PluginLibrary& library, const std::string& pluginId);
};

} // namespace clap_validator
