#pragma once

#include <vector>
#include <filesystem>

namespace clap_validator {
namespace commands {

// List all installed CLAP plugins
int listPlugins(bool json);

// List all available presets for plugins
int listPresets(bool json, const std::vector<std::filesystem::path>& paths);

// List all available test cases
int listTests(bool json);

} // namespace commands
} // namespace clap_validator
