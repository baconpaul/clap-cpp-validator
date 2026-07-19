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

#ifndef CLAPVALCPP_SRC_PLUGIN_PRESET_DISCOVERY_H
#define CLAPVALCPP_SRC_PLUGIN_PRESET_DISCOVERY_H

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <clap/clap.h>

namespace clap_validator
{

class PluginLibrary;
class Indexer; // defined in the .cpp

// A preset location: either an absolute path to a file or directory, or the plugin's own internal
// storage (CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN).
struct LocationValue
{
    bool internal = false; // true => CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN
    std::string path;      // an absolute path when !internal

    // Reconstruct the (kind, location-pointer) pair for a CLAP call. The pointer is valid for the
    // lifetime of this object.
    uint32_t rawKind() const;
    const char *rawLocation() const;

    std::string display() const;
    std::string fileName() const; // base name, or "<plugin>" for internal storage
};

// A declared preset filetype.
struct FileType
{
    std::string name;
    std::optional<std::string> description;
    std::string extension; // without a leading period
};

// A declared preset location.
struct DiscoveredLocation
{
    std::string name;
    LocationValue value;
};

// A plugin ABI + id pair a preset can be loaded into.
struct PluginId
{
    std::string abi; // "clap" for CLAP plugins
    std::string id;
};

// A single preset discovered while crawling a location.
struct DiscoveredPreset
{
    LocationValue location;
    std::optional<std::string> loadKey; // set for presets inside a container file
    std::string name;
    std::vector<PluginId> pluginIds;
};

// Metadata (descriptor) for a preset discovery provider.
struct ProviderMetadata
{
    uint32_t versionMajor = 0;
    uint32_t versionMinor = 0;
    uint32_t versionRevision = 0;
    std::string id;
    std::string name;
    std::optional<std::string> vendor;

    bool operator==(const ProviderMetadata &o) const;
    bool operator!=(const ProviderMetadata &o) const { return !(*this == o); }

    clap_version_t clapVersion() const
    {
        return clap_version_t{versionMajor, versionMinor, versionRevision};
    }

    static ProviderMetadata
    fromDescriptor(const clap_preset_discovery_provider_descriptor_t *descriptor);
};

// A created and initialized preset discovery provider. Reads the provider's declared file types and
// locations during construction and destroys the provider when this object is destroyed.
class Provider
{
  public:
    ~Provider();

    Provider(const Provider &) = delete;
    Provider &operator=(const Provider &) = delete;

    // Create a provider from a factory and run its init(), reading the declared data. Throws
    // std::runtime_error on any failure.
    static std::unique_ptr<Provider> create(const clap_preset_discovery_factory_t *factory,
                                            const std::string &providerId);

    const std::vector<FileType> &fileTypes() const { return fileTypes_; }
    const std::vector<DiscoveredLocation> &locations() const { return locations_; }

    // Get the descriptor stored on the provider's `desc` field.
    ProviderMetadata descriptor() const;

    // Crawl a declared location and append every discovered preset to `out`. Throws on any error
    // reported by the plugin.
    void crawlLocation(const DiscoveredLocation &location, std::vector<DiscoveredPreset> &out);

  private:
    Provider(const clap_preset_discovery_provider_t *provider, std::unique_ptr<Indexer> indexer);

    const clap_preset_discovery_provider_t *provider_;
    std::unique_ptr<Indexer> indexer_;
    std::vector<FileType> fileTypes_;
    std::vector<DiscoveredLocation> locations_;
};

// A wrapper around a library's preset discovery factory.
class PresetDiscoveryFactory
{
  public:
    // Returns nullopt if the library does not expose a preset discovery factory.
    static std::optional<PresetDiscoveryFactory> fromLibrary(const PluginLibrary &library);

    // Metadata for every provider the factory advertises. Throws on a null descriptor or duplicate
    // provider ids.
    std::vector<ProviderMetadata> metadata() const;

    // Create a provider from one of the metadata entries. Throws if the provider's CLAP version is
    // unsupported or creation fails.
    std::unique_ptr<Provider> createProvider(const ProviderMetadata &metadata) const;

  private:
    explicit PresetDiscoveryFactory(const clap_preset_discovery_factory_t *factory)
        : factory_(factory)
    {
    }

    const clap_preset_discovery_factory_t *factory_;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_PLUGIN_PRESET_DISCOVERY_H
