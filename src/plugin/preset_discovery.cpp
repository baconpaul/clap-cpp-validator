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
#include "preset_discovery.h"
#include "library.h"
#include "../util.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <thread>

namespace clap_validator
{

namespace
{
std::optional<std::string> optString(const char *ptr)
{
    if (!ptr || ptr[0] == '\0')
    {
        return std::nullopt;
    }
    return std::string(ptr);
}

std::string lowerTrim(const std::string &s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    std::string result =
        begin == std::string::npos ? std::string() : s.substr(begin, end - begin + 1);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
} // namespace

// ---------------------------------------------------------------------------
// LocationValue

uint32_t LocationValue::rawKind() const
{
    return internal ? CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN : CLAP_PRESET_DISCOVERY_LOCATION_FILE;
}

const char *LocationValue::rawLocation() const { return internal ? nullptr : path.c_str(); }

std::string LocationValue::display() const
{
    if (internal)
    {
        return "CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN";
    }
    return "CLAP_PRESET_DISCOVERY_LOCATION_FILE with path '" + path + "'";
}

std::string LocationValue::fileName() const
{
    if (internal)
    {
        return "<plugin>";
    }
    return std::filesystem::path(path).filename().string();
}

// ---------------------------------------------------------------------------
// ProviderMetadata

bool ProviderMetadata::operator==(const ProviderMetadata &o) const
{
    return versionMajor == o.versionMajor && versionMinor == o.versionMinor &&
           versionRevision == o.versionRevision && id == o.id && name == o.name &&
           vendor == o.vendor;
}

ProviderMetadata
ProviderMetadata::fromDescriptor(const clap_preset_discovery_provider_descriptor_t *descriptor)
{
    if (!descriptor)
    {
        throw std::runtime_error("The preset discovery provider descriptor is a null pointer.");
    }
    ProviderMetadata metadata;
    metadata.versionMajor = descriptor->clap_version.major;
    metadata.versionMinor = descriptor->clap_version.minor;
    metadata.versionRevision = descriptor->clap_version.revision;
    if (!descriptor->id)
    {
        throw std::runtime_error("The preset provider descriptor's 'id' field is null.");
    }
    metadata.id = descriptor->id;
    if (!descriptor->name)
    {
        throw std::runtime_error("The preset provider descriptor's 'name' field is null.");
    }
    metadata.name = descriptor->name;
    metadata.vendor = optString(descriptor->vendor);
    return metadata;
}

// ---------------------------------------------------------------------------
// Indexer: the host side of the preset discovery handshake. The plugin declares its file types and
// locations into this object during provider->init().

class Indexer
{
  public:
    Indexer() : expectedThreadId_(std::this_thread::get_id())
    {
        vtable_.clap_version = CLAP_VERSION;
        vtable_.name = "clap-validator";
        vtable_.vendor = "clap-validator authors";
        vtable_.url = "https://github.com/baconpaul/clap-cpp-validator";
        vtable_.version = "1.0.0";
        vtable_.indexer_data = this;
        vtable_.declare_filetype = &Indexer::declareFiletype;
        vtable_.declare_location = &Indexer::declareLocation;
        vtable_.declare_soundpack = &Indexer::declareSoundpack;
        vtable_.get_extension = &Indexer::getExtension;
    }

    Indexer(const Indexer &) = delete;
    Indexer &operator=(const Indexer &) = delete;
    Indexer(Indexer &&) = delete;
    Indexer &operator=(Indexer &&) = delete;

    const clap_preset_discovery_indexer_t *vtable() const { return &vtable_; }

    std::vector<FileType> takeFileTypes() { return std::move(fileTypes_); }
    std::vector<DiscoveredLocation> takeLocations() { return std::move(locations_); }
    const std::optional<std::string> &error() const { return error_; }

  private:
    void setError(const std::string &message)
    {
        if (!error_)
        {
            error_ = message;
        }
    }

    void assertSameThread(const char *fn)
    {
        if (std::this_thread::get_id() != expectedThreadId_)
        {
            setError(std::string(fn) + " was called from a different thread than the one the "
                                       "indexer was created on.");
        }
    }

    static Indexer *from(const clap_preset_discovery_indexer_t *indexer)
    {
        return indexer ? static_cast<Indexer *>(indexer->indexer_data) : nullptr;
    }

    static bool CLAP_ABI declareFiletype(const clap_preset_discovery_indexer_t *indexer,
                                         const clap_preset_discovery_filetype_t *filetype)
    {
        Indexer *self = from(indexer);
        if (!self || !filetype)
        {
            return false;
        }
        self->assertSameThread("clap_preset_discovery_indexer::declare_filetype()");

        if (!filetype->name)
        {
            self->setError("A declared preset filetype has no name.");
            return false;
        }
        FileType fileType;
        fileType.name = filetype->name;
        fileType.description = optString(filetype->description);
        // The spec allows a null/empty extension to mean "match every file".
        fileType.extension = filetype->file_extension ? filetype->file_extension : "";
        if (!fileType.extension.empty() && fileType.extension.front() == '.')
        {
            self->setError("Preset file extensions may not start with a period, so '" +
                           fileType.extension + "' is not allowed.");
            return false;
        }
        self->fileTypes_.push_back(std::move(fileType));
        return true;
    }

    static bool CLAP_ABI declareLocation(const clap_preset_discovery_indexer_t *indexer,
                                         const clap_preset_discovery_location_t *location)
    {
        Indexer *self = from(indexer);
        if (!self || !location)
        {
            return false;
        }
        self->assertSameThread("clap_preset_discovery_indexer::declare_location()");

        if (!location->name)
        {
            self->setError("A declared preset location has no name.");
            return false;
        }
        LocationValue value;
        if (location->kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE)
        {
            if (!location->location)
            {
                self->setError("A CLAP_PRESET_DISCOVERY_LOCATION_FILE location has a null path.");
                return false;
            }
            value.internal = false;
            value.path = location->location;
            if (value.path.empty() || value.path.front() != '/')
            {
                self->setError("The preset location path '" + value.path + "' should be absolute.");
                return false;
            }
        }
        else if (location->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
        {
            if (location->location)
            {
                self->setError("A CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN location must have a null "
                               "path.");
                return false;
            }
            value.internal = true;
        }
        else
        {
            self->setError("Unknown preset location kind " + std::to_string(location->kind) + ".");
            return false;
        }

        self->locations_.push_back(DiscoveredLocation{location->name, std::move(value)});
        return true;
    }

    static bool CLAP_ABI declareSoundpack(const clap_preset_discovery_indexer_t *indexer,
                                          const clap_preset_discovery_soundpack_t *soundpack)
    {
        Indexer *self = from(indexer);
        if (!self || !soundpack)
        {
            return false;
        }
        self->assertSameThread("clap_preset_discovery_indexer::declare_soundpack()");
        // Soundpacks are not used by any of the checks, so we simply accept them.
        return true;
    }

    static const void *CLAP_ABI getExtension(const clap_preset_discovery_indexer_t *, const char *)
    {
        // There are currently no preset discovery indexer extensions.
        return nullptr;
    }

    clap_preset_discovery_indexer_t vtable_{};
    std::thread::id expectedThreadId_;
    std::vector<FileType> fileTypes_;
    std::vector<DiscoveredLocation> locations_;
    std::optional<std::string> error_;
};

namespace
{
// MetadataReceiver: the host side of a single get_metadata() call. The plugin declares one or more
// presets into this object; finalized presets are appended to `out`.
class MetadataReceiver
{
  public:
    MetadataReceiver(const LocationValue &location, std::vector<DiscoveredPreset> &out)
        : expectedThreadId_(std::this_thread::get_id()), location_(&location), out_(&out)
    {
        vtable_.receiver_data = this;
        vtable_.on_error = &MetadataReceiver::onError;
        vtable_.begin_preset = &MetadataReceiver::beginPreset;
        vtable_.add_plugin_id = &MetadataReceiver::addPluginId;
        vtable_.set_soundpack_id = &MetadataReceiver::setSoundpackId;
        vtable_.set_flags = &MetadataReceiver::setFlags;
        vtable_.add_creator = &MetadataReceiver::addCreator;
        vtable_.set_description = &MetadataReceiver::setDescription;
        vtable_.set_timestamps = &MetadataReceiver::setTimestamps;
        vtable_.add_feature = &MetadataReceiver::addFeature;
        vtable_.add_extra_info = &MetadataReceiver::addExtraInfo;
    }

    MetadataReceiver(const MetadataReceiver &) = delete;
    MetadataReceiver &operator=(const MetadataReceiver &) = delete;
    MetadataReceiver(MetadataReceiver &&) = delete;
    MetadataReceiver &operator=(MetadataReceiver &&) = delete;

    const clap_preset_discovery_metadata_receiver_t *vtable() const { return &vtable_; }
    const std::optional<std::string> &error() const { return error_; }

    // Finalize any pending preset. Call once after get_metadata() returns.
    void finish() { finalizeCurrent(); }

  private:
    enum class Mode
    {
        Unknown,
        Single,
        Container
    };

    void setError(const std::string &message)
    {
        if (!error_)
        {
            error_ = message;
        }
    }

    void assertSameThread(const char *fn)
    {
        if (std::this_thread::get_id() != expectedThreadId_)
        {
            setError(std::string(fn) + " was called from a different thread than the one the "
                                       "metadata receiver was created on.");
        }
    }

    void finalizeCurrent()
    {
        if (!hasCurrent_)
        {
            return;
        }
        hasCurrent_ = false;
        if (error_)
        {
            return;
        }
        if (current_.pluginIds.empty())
        {
            setError("The preset '" + current_.name + "' was defined without a plugin ID.");
            return;
        }
        out_->push_back(std::move(current_));
    }

    static MetadataReceiver *from(const clap_preset_discovery_metadata_receiver_t *receiver)
    {
        return receiver ? static_cast<MetadataReceiver *>(receiver->receiver_data) : nullptr;
    }

    static void CLAP_ABI onError(const clap_preset_discovery_metadata_receiver_t *receiver,
                                 int32_t osError, const char *message)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::on_error()");
        self->setError("on_error() was called for OS error code " + std::to_string(osError) +
                       " with message: " + (message ? message : "<null>"));
    }

    static bool CLAP_ABI beginPreset(const clap_preset_discovery_metadata_receiver_t *receiver,
                                     const char *name, const char *loadKey)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return false;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::begin_preset()");
        if (self->error_)
        {
            return false;
        }

        std::optional<std::string> nameOpt = optString(name);
        std::optional<std::string> loadKeyOpt = optString(loadKey);

        switch (self->mode_)
        {
        case Mode::Single:
            self->setError("'begin_preset()' was called a second time for a non-container preset "
                           "file. This is invalid behavior.");
            return false;
        case Mode::Container:
            if (!loadKeyOpt)
            {
                self->setError("'begin_preset()' was called with a load key the first time and "
                               "without one the second time. This is invalid behavior.");
                return false;
            }
            break;
        case Mode::Unknown:
            break;
        }

        std::string presetName;
        if (nameOpt)
        {
            presetName = *nameOpt;
        }
        else if (!loadKeyOpt)
        {
            presetName = self->location_->fileName();
        }
        else
        {
            self->setError("Container presets must specify a preset name.");
            return false;
        }

        if (self->mode_ == Mode::Unknown)
        {
            self->mode_ = loadKeyOpt ? Mode::Container : Mode::Single;
        }
        if (loadKeyOpt)
        {
            // Finalize the previous preset in this container before starting a new one.
            self->finalizeCurrent();
            if (self->error_)
            {
                return false;
            }
        }

        self->current_ = DiscoveredPreset{};
        self->current_.location = *self->location_;
        self->current_.loadKey = loadKeyOpt;
        self->current_.name = presetName;
        self->hasCurrent_ = true;
        return true;
    }

    // Returns the current preset, or records an error and returns nullptr if begin_preset() has not
    // been called.
    DiscoveredPreset *currentOrError(const char *fn)
    {
        if (!hasCurrent_)
        {
            setError(std::string(fn) + " was called with no preceding 'begin_preset()' call.");
            return nullptr;
        }
        return &current_;
    }

    static void CLAP_ABI addPluginId(const clap_preset_discovery_metadata_receiver_t *receiver,
                                     const clap_universal_plugin_id_t *pluginId)
    {
        MetadataReceiver *self = from(receiver);
        if (!self || !pluginId)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::add_plugin_id()");
        if (!pluginId->abi || !pluginId->id)
        {
            self->setError("add_plugin_id() was called with a null abi or id field.");
            return;
        }
        DiscoveredPreset *current =
            self->currentOrError("clap_preset_discovery_metadata_receiver::add_plugin_id()");
        if (!current)
        {
            return;
        }
        std::string abi = pluginId->abi;
        if (abi == "clap")
        {
            current->pluginIds.push_back(PluginId{"clap", pluginId->id});
        }
        else if (lowerTrim(abi) == "clap")
        {
            self->setError("'" + abi +
                           "' was provided as the plugin ABI to add_plugin_id(). This is probably "
                           "a typo; the expected value is 'clap' in all lowercase.");
        }
        else
        {
            current->pluginIds.push_back(PluginId{abi, pluginId->id});
        }
    }

    static void CLAP_ABI setSoundpackId(const clap_preset_discovery_metadata_receiver_t *receiver,
                                        const char *)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::set_soundpack_id()");
        self->currentOrError("clap_preset_discovery_metadata_receiver::set_soundpack_id()");
    }

    static void CLAP_ABI setFlags(const clap_preset_discovery_metadata_receiver_t *receiver,
                                  uint32_t)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::set_flags()");
        self->currentOrError("clap_preset_discovery_metadata_receiver::set_flags()");
    }

    static void CLAP_ABI addCreator(const clap_preset_discovery_metadata_receiver_t *receiver,
                                    const char *creator)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::add_creator()");
        if (!creator)
        {
            self->setError("add_creator() was called with a null creator.");
            return;
        }
        self->currentOrError("clap_preset_discovery_metadata_receiver::add_creator()");
    }

    static void CLAP_ABI setDescription(const clap_preset_discovery_metadata_receiver_t *receiver,
                                        const char *description)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::set_description()");
        if (!description)
        {
            self->setError("set_description() was called with a null description.");
            return;
        }
        self->currentOrError("clap_preset_discovery_metadata_receiver::set_description()");
    }

    static void CLAP_ABI setTimestamps(const clap_preset_discovery_metadata_receiver_t *receiver,
                                       clap_timestamp creationTime, clap_timestamp modificationTime)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::set_timestamps()");
        if (creationTime == CLAP_TIMESTAMP_UNKNOWN && modificationTime == CLAP_TIMESTAMP_UNKNOWN)
        {
            self->setError("set_timestamps() was called with both timestamps set to "
                           "CLAP_TIMESTAMP_UNKNOWN.");
            return;
        }
        self->currentOrError("clap_preset_discovery_metadata_receiver::set_timestamps()");
    }

    static void CLAP_ABI addFeature(const clap_preset_discovery_metadata_receiver_t *receiver,
                                    const char *feature)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::add_feature()");
        if (!feature)
        {
            self->setError("add_feature() was called with a null feature.");
            return;
        }
        self->currentOrError("clap_preset_discovery_metadata_receiver::add_feature()");
    }

    static void CLAP_ABI addExtraInfo(const clap_preset_discovery_metadata_receiver_t *receiver,
                                      const char *key, const char *value)
    {
        MetadataReceiver *self = from(receiver);
        if (!self)
        {
            return;
        }
        self->assertSameThread("clap_preset_discovery_metadata_receiver::add_extra_info()");
        if (!key || !value)
        {
            self->setError("add_extra_info() was called with a null key or value.");
            return;
        }
        self->currentOrError("clap_preset_discovery_metadata_receiver::add_extra_info()");
    }

    clap_preset_discovery_metadata_receiver_t vtable_{};
    std::thread::id expectedThreadId_;
    const LocationValue *location_;
    std::vector<DiscoveredPreset> *out_;
    std::optional<std::string> error_;
    Mode mode_ = Mode::Unknown;
    bool hasCurrent_ = false;
    DiscoveredPreset current_;
};
} // namespace

// ---------------------------------------------------------------------------
// Provider

Provider::Provider(const clap_preset_discovery_provider_t *provider,
                   std::unique_ptr<Indexer> indexer)
    : provider_(provider), indexer_(std::move(indexer))
{
}

Provider::~Provider()
{
    if (provider_ && provider_->destroy)
    {
        provider_->destroy(provider_);
    }
}

std::unique_ptr<Provider> Provider::create(const clap_preset_discovery_factory_t *factory,
                                           const std::string &providerId)
{
    auto indexer = std::make_unique<Indexer>();

    const clap_preset_discovery_provider_t *provider =
        factory->create(factory, indexer->vtable(), providerId.c_str());
    if (!provider)
    {
        throw std::runtime_error(
            "'clap_preset_discovery_factory::create()' returned a null pointer for the provider "
            "with ID '" +
            providerId + "'.");
    }

    if (!provider->init || !provider->init(provider))
    {
        if (provider->destroy)
        {
            provider->destroy(provider);
        }
        throw std::runtime_error("'clap_preset_discovery_provider::init()' returned false for the "
                                 "provider with ID '" +
                                 providerId + "'.");
    }

    if (auto err = indexer->error())
    {
        if (provider->destroy)
        {
            provider->destroy(provider);
        }
        throw std::runtime_error("Error during preset discovery indexer callbacks for provider '" +
                                 providerId + "': " + *err);
    }

    auto fileTypes = indexer->takeFileTypes();
    auto locations = indexer->takeLocations();

    std::unique_ptr<Provider> result(new Provider(provider, std::move(indexer)));
    result->fileTypes_ = std::move(fileTypes);
    result->locations_ = std::move(locations);
    return result;
}

ProviderMetadata Provider::descriptor() const
{
    if (!provider_->desc)
    {
        throw std::runtime_error(
            "The 'desc' field on the 'clap_preset_discovery_provider' struct is a null pointer.");
    }
    return ProviderMetadata::fromDescriptor(provider_->desc);
}

void Provider::crawlLocation(const DiscoveredLocation &location, std::vector<DiscoveredPreset> &out)
{
    auto crawlOne = [&](const LocationValue &value)
    {
        MetadataReceiver receiver(value, out);
        bool success = provider_->get_metadata(provider_, value.rawKind(), value.rawLocation(),
                                               receiver.vtable());
        receiver.finish();
        if (auto err = receiver.error())
        {
            throw std::runtime_error("Error while fetching metadata for " + value.display() + ": " +
                                     *err);
        }
        if (!success)
        {
            throw std::runtime_error(
                "The preset provider returned false when fetching metadata for " + value.display() +
                ".");
        }
    };

    if (location.value.internal)
    {
        crawlOne(location.value);
        return;
    }

    std::error_code ec;
    std::filesystem::file_status status = std::filesystem::status(location.value.path, ec);
    if (ec)
    {
        throw std::runtime_error("Could not query the declared preset location '" +
                                 location.value.path + "': " + ec.message());
    }

    if (!std::filesystem::is_directory(status))
    {
        crawlOne(location.value);
        return;
    }

    // Directory: walk it, filtering by the declared file extensions (if any).
    std::set<std::string> allowedExtensions;
    for (const auto &fileType : fileTypes_)
    {
        if (!fileType.extension.empty())
        {
            allowedExtensions.insert(fileType.extension);
        }
    }

    std::filesystem::recursive_directory_iterator it(
        location.value.path, std::filesystem::directory_options::follow_directory_symlink, ec);
    const std::filesystem::recursive_directory_iterator end;
    while (!ec && it != end)
    {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc)
        {
            std::string extension = it->path().extension().string();
            if (!extension.empty() && extension.front() == '.')
            {
                extension = extension.substr(1);
            }
            if (allowedExtensions.empty() ||
                allowedExtensions.find(extension) != allowedExtensions.end())
            {
                LocationValue fileLocation;
                fileLocation.internal = false;
                fileLocation.path = it->path().string();
                crawlOne(fileLocation);
            }
        }
        it.increment(ec);
    }
}

// ---------------------------------------------------------------------------
// PresetDiscoveryFactory

std::optional<PresetDiscoveryFactory>
PresetDiscoveryFactory::fromLibrary(const PluginLibrary &library)
{
    const clap_plugin_entry_t *entry = library.getEntryPoint();
    if (!entry || !entry->get_factory)
    {
        return std::nullopt;
    }

    const void *factory = entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID);
    if (!factory)
    {
        factory = entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
    }
    if (!factory)
    {
        return std::nullopt;
    }
    return PresetDiscoveryFactory(static_cast<const clap_preset_discovery_factory_t *>(factory));
}

std::vector<ProviderMetadata> PresetDiscoveryFactory::metadata() const
{
    std::vector<ProviderMetadata> result;
    uint32_t count = factory_->count(factory_);
    std::set<std::string> seenIds;
    for (uint32_t i = 0; i < count; ++i)
    {
        const clap_preset_discovery_provider_descriptor_t *descriptor =
            factory_->get_descriptor(factory_, i);
        if (!descriptor)
        {
            throw std::runtime_error(
                "The preset discovery factory returned a null descriptor at index " +
                std::to_string(i) + ".");
        }
        ProviderMetadata metadata = ProviderMetadata::fromDescriptor(descriptor);
        if (!seenIds.insert(metadata.id).second)
        {
            throw std::runtime_error(
                "The preset discovery factory contains multiple providers with the ID '" +
                metadata.id + "'.");
        }
        result.push_back(std::move(metadata));
    }
    return result;
}

std::unique_ptr<Provider>
PresetDiscoveryFactory::createProvider(const ProviderMetadata &metadata) const
{
    if (!isVersionCompatible(metadata.clapVersion()))
    {
        throw std::runtime_error("The preset provider with ID '" + metadata.id +
                                 "' has an unsupported CLAP version.");
    }
    return Provider::create(factory_, metadata.id);
}

} // namespace clap_validator
