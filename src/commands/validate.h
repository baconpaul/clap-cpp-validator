#pragma once

#include <vector>
#include <string>
#include <optional>
#include <filesystem>

namespace clap_validator {

// Settings for the validator
struct ValidatorSettings {
    std::vector<std::filesystem::path> paths;
    std::optional<std::string> pluginId;
    std::optional<std::string> testFilter;
    bool invertFilter = false;
    bool json = false;
    bool onlyFailed = false;
    bool inProcess = true;  // Default to in-process for simplicity
};

namespace commands {

// Run validation on the specified plugins
int validate(const ValidatorSettings& settings);

} // namespace commands
} // namespace clap_validator
