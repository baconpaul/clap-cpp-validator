#include "util.h"
#include <stdexcept>
#include <cstdlib>

namespace clap_validator
{

std::optional<std::string> cstrToOptionalString(const char *str)
{
    if (str == nullptr || str[0] == '\0')
    {
        return std::nullopt;
    }
    return std::string(str);
}

std::string cstrToString(const char *str)
{
    if (str == nullptr)
    {
        throw std::runtime_error("Null pointer passed to cstrToString");
    }
    return std::string(str);
}

std::vector<std::string> cstrArrayToVector(const char *const *arr)
{
    std::vector<std::string> result;
    if (arr == nullptr)
    {
        return result;
    }
    while (*arr != nullptr)
    {
        result.emplace_back(*arr);
        ++arr;
    }
    return result;
}

std::filesystem::path getValidatorTempDir()
{
    std::filesystem::path tempDir;

#ifdef _WIN32
    const char *temp = std::getenv("TEMP");
    if (temp)
    {
        tempDir = temp;
    }
    else
    {
        tempDir = std::filesystem::temp_directory_path();
    }
#else
    tempDir = std::filesystem::temp_directory_path();
#endif

    return tempDir / "clap-validator";
}

bool isVersionCompatible(const clap_version_t &version)
{
    return clap_version_is_compatible(version);
}

} // namespace clap_validator
