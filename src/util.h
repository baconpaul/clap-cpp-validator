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

#ifndef CLAPVALCPP_SRC_UTIL_H
#define CLAPVALCPP_SRC_UTIL_H

#include <string>
#include <optional>
#include <vector>
#include <filesystem>
#include <clap/clap.h>

namespace clap_validator
{

// Convert a C string pointer to a std::string, returning empty optional if null or empty
std::optional<std::string> cstrToOptionalString(const char *str);

// Convert a C string pointer to a std::string, throws if null
std::string cstrToString(const char *str);

// Convert a null-terminated array of C strings to a vector of strings
std::vector<std::string> cstrArrayToVector(const char *const *arr);

// Get the temporary directory for validator artifacts
std::filesystem::path getValidatorTempDir();

// Check if a CLAP version is compatible
bool isVersionCompatible(const clap_version_t &version);

// Safe wrapper for calling CLAP functions that checks for null pointers
template <typename Func, typename... Args>
auto safeCall(Func func, Args &&...args) -> decltype(func(std::forward<Args>(args)...))
{
    if (func)
    {
        return func(std::forward<Args>(args)...);
    }
    // Return default value for the return type
    return decltype(func(std::forward<Args>(args)...))();
}

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_UTIL_H
