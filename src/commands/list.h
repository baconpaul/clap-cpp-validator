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

#ifndef CLAPVALCPP_SRC_COMMANDS_LIST_H
#define CLAPVALCPP_SRC_COMMANDS_LIST_H

#include <vector>
#include <filesystem>

namespace clap_validator
{
namespace commands
{

// List all installed CLAP plugins
int listPlugins(bool json);

// List all available presets for plugins
int listPresets(bool json, const std::vector<std::filesystem::path> &paths);

// List all available test cases
int listTests(bool json);

} // namespace commands
} // namespace clap_validator

#endif // CLAPVALCPP_SRC_COMMANDS_LIST_H
