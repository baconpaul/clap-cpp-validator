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

#ifndef CLAPVALCPP_SRC_PLUGIN_LIBRARY_H
#define CLAPVALCPP_SRC_PLUGIN_LIBRARY_H

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <filesystem>
#include <clap/clap.h>

namespace clap_validator
{

// Metadata for a single plugin within a CLAP plugin library
struct PluginMetadata
{
    std::string id;
    std::string name;
    std::optional<std::string> version;
    std::optional<std::string> vendor;
    std::optional<std::string> description;
    std::optional<std::string> manualUrl;
    std::optional<std::string> supportUrl;
    std::vector<std::string> features;

    static PluginMetadata fromDescriptor(const clap_plugin_descriptor_t *descriptor);
};

// Metadata for a CLAP plugin library, which may contain multiple plugins
struct PluginLibraryMetadata
{
    uint32_t versionMajor;
    uint32_t versionMinor;
    uint32_t versionRevision;
    std::vector<PluginMetadata> plugins;

    clap_version_t clapVersion() const
    {
        return clap_version_t{versionMajor, versionMinor, versionRevision};
    }
};

// Forward declarations
class Host;
class Plugin;

// A CLAP plugin library built from a CLAP plugin's entry point
class PluginLibrary
{
  public:
    ~PluginLibrary();

    // Load a CLAP plugin from a path to a .clap file or bundle
    static std::unique_ptr<PluginLibrary> load(const std::filesystem::path &path);

    // Get the path to this plugin
    const std::filesystem::path &pluginPath() const { return pluginPath_; }

    // Get the metadata for all plugins stored in this plugin library
    PluginLibraryMetadata metadata() const;

    // Returns whether or not a factory with the specified ID exists
    bool factoryExists(const std::string &factoryId) const;

    // Try to create the plugin with the given ID
    std::unique_ptr<Plugin> createPlugin(const std::string &id, std::shared_ptr<Host> host);

    // Get the plugin factory (for tests that need direct access)
    const clap_plugin_factory_t *getPluginFactory() const;

    // Get the entry point (for tests)
    const clap_plugin_entry_t *getEntryPoint() const { return entryPoint_; }

  private:
    PluginLibrary(const std::filesystem::path &path, void *libraryHandle,
                  const clap_plugin_entry_t *entryPoint);

    std::filesystem::path pluginPath_;
    void *libraryHandle_;
    const clap_plugin_entry_t *entryPoint_;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_PLUGIN_LIBRARY_H
