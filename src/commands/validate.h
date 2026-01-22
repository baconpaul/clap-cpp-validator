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

#ifndef CLAPVALCPP_SRC_COMMANDS_VALIDATE_H
#define CLAPVALCPP_SRC_COMMANDS_VALIDATE_H

#include <vector>
#include <string>
#include <optional>
#include <filesystem>

namespace clap_validator
{

// Settings for the validator
struct ValidatorSettings
{
    std::vector<std::filesystem::path> paths;
    std::optional<std::string> pluginId;
    std::optional<std::string> testFilter;
    bool invertFilter = false;
    bool json = false;
    bool onlyFailed = false;
    bool inProcess = true; // Default to in-process for simplicity
};

namespace commands
{

// Run validation on the specified plugins
int validate(const ValidatorSettings &settings);

} // namespace commands
} // namespace clap_validator

#endif // CLAPVALCPP_SRC_COMMANDS_VALIDATE_H
