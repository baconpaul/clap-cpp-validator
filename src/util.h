#pragma once

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
