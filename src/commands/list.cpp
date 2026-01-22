#include "list.h"
#include "../plugin/library.h"
#include "../tests/plugin_library_tests.h"
#include "../tests/plugin_tests.h"
#include <iostream>
#include <vector>

namespace clap_validator {
namespace commands {

// Get standard CLAP plugin directories for the current platform
std::vector<std::filesystem::path> getPluginSearchPaths() {
    std::vector<std::filesystem::path> paths;
    
#ifdef __APPLE__
    // macOS paths
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::filesystem::path(home) / "Library/Audio/Plug-Ins/CLAP");
    }
    paths.push_back("/Library/Audio/Plug-Ins/CLAP");
#elif defined(_WIN32)
    // Windows paths
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        paths.push_back(std::filesystem::path(localAppData) / "Programs/Common/CLAP");
    }
    if (const char* commonProgramFiles = std::getenv("COMMONPROGRAMFILES")) {
        paths.push_back(std::filesystem::path(commonProgramFiles) / "CLAP");
    }
#else
    // Linux paths
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::filesystem::path(home) / ".clap");
    }
    paths.push_back("/usr/lib/clap");
#endif
    
    return paths;
}

// Find all .clap files in the given directories
std::vector<std::filesystem::path> findPlugins(const std::vector<std::filesystem::path>& searchPaths) {
    std::vector<std::filesystem::path> plugins;
    
    for (const auto& searchPath : searchPaths) {
        if (!std::filesystem::exists(searchPath)) {
            continue;
        }
        
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(searchPath)) {
                if (entry.is_regular_file() || entry.is_directory()) {
                    if (entry.path().extension() == ".clap") {
                        plugins.push_back(entry.path());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not search " << searchPath << ": " << e.what() << std::endl;
        }
    }
    
    return plugins;
}

int listPlugins(bool json) {
    auto searchPaths = getPluginSearchPaths();
    auto pluginPaths = findPlugins(searchPaths);
    
    if (json) {
        std::cout << "{\n  \"plugins\": [\n";
        bool first = true;
        
        for (const auto& path : pluginPaths) {
            try {
                auto library = PluginLibrary::load(path);
                auto metadata = library->metadata();
                
                for (const auto& plugin : metadata.plugins) {
                    if (!first) std::cout << ",\n";
                    first = false;
                    
                    std::cout << "    {\n";
                    std::cout << "      \"path\": \"" << path.string() << "\",\n";
                    std::cout << "      \"id\": \"" << plugin.id << "\",\n";
                    std::cout << "      \"name\": \"" << plugin.name << "\",\n";
                    std::cout << "      \"version\": \"" << plugin.version.value_or("") << "\",\n";
                    std::cout << "      \"vendor\": \"" << plugin.vendor.value_or("") << "\"\n";
                    std::cout << "    }";
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not load " << path << ": " << e.what() << std::endl;
            }
        }
        
        std::cout << "\n  ]\n}\n";
    } else {
        std::cout << "Installed CLAP plugins:\n\n";
        
        for (const auto& path : pluginPaths) {
            try {
                auto library = PluginLibrary::load(path);
                auto metadata = library->metadata();
                
                for (const auto& plugin : metadata.plugins) {
                    std::cout << "  " << plugin.name;
                    if (plugin.version) {
                        std::cout << " v" << *plugin.version;
                    }
                    if (plugin.vendor) {
                        std::cout << " by " << *plugin.vendor;
                    }
                    std::cout << "\n";
                    std::cout << "    ID: " << plugin.id << "\n";
                    std::cout << "    Path: " << path.string() << "\n\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not load " << path << ": " << e.what() << std::endl;
            }
        }
        
        if (pluginPaths.empty()) {
            std::cout << "  No plugins found.\n";
        }
    }
    
    return 0;
}

int listPresets(bool json, const std::vector<std::filesystem::path>& paths) {
    // Preset discovery is a more complex feature - for now just indicate it's not implemented
    if (json) {
        std::cout << "{\n  \"presets\": [],\n  \"note\": \"Preset discovery not yet implemented\"\n}\n";
    } else {
        std::cout << "Preset discovery not yet implemented.\n";
    }
    return 0;
}

int listTests(bool json) {
    auto libraryTests = PluginLibraryTests::getAllTests();
    auto pluginTests = PluginTests::getAllTests();
    
    if (json) {
        std::cout << "{\n";
        std::cout << "  \"plugin-library-tests\": {\n";
        bool first = true;
        for (const auto& test : libraryTests) {
            if (!first) std::cout << ",\n";
            first = false;
            std::cout << "    \"" << test.name << "\": \"" << test.description << "\"";
        }
        std::cout << "\n  },\n";
        
        std::cout << "  \"plugin-tests\": {\n";
        first = true;
        for (const auto& test : pluginTests) {
            if (!first) std::cout << ",\n";
            first = false;
            std::cout << "    \"" << test.name << "\": \"" << test.description << "\"";
        }
        std::cout << "\n  }\n";
        std::cout << "}\n";
    } else {
        std::cout << "Plugin Library Tests:\n";
        for (const auto& test : libraryTests) {
            std::cout << "  " << test.name << "\n";
            std::cout << "    " << test.description << "\n\n";
        }
        
        std::cout << "Plugin Tests:\n";
        for (const auto& test : pluginTests) {
            std::cout << "  " << test.name << "\n";
            std::cout << "    " << test.description << "\n\n";
        }
    }
    
    return 0;
}

} // namespace commands
} // namespace clap_validator
