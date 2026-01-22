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
#include "library.h"
#include "host.h"
#include "instance.h"
#include "../util.h"

#include <stdexcept>
#include <set>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace clap_validator
{

PluginMetadata PluginMetadata::fromDescriptor(const clap_plugin_descriptor_t *descriptor)
{
    if (!descriptor)
    {
        throw std::runtime_error("Null plugin descriptor");
    }

    PluginMetadata metadata;
    metadata.id = cstrToString(descriptor->id);
    metadata.name = cstrToString(descriptor->name);
    metadata.version = cstrToOptionalString(descriptor->version);
    metadata.vendor = cstrToOptionalString(descriptor->vendor);
    metadata.description = cstrToOptionalString(descriptor->description);
    metadata.manualUrl = cstrToOptionalString(descriptor->manual_url);
    metadata.supportUrl = cstrToOptionalString(descriptor->support_url);
    metadata.features = cstrArrayToVector(descriptor->features);

    return metadata;
}

PluginLibrary::PluginLibrary(const std::filesystem::path &path, void *libraryHandle,
                             const clap_plugin_entry_t *entryPoint)
    : pluginPath_(path), libraryHandle_(libraryHandle), entryPoint_(entryPoint)
{
}

PluginLibrary::~PluginLibrary()
{
    if (entryPoint_)
    {
        entryPoint_->deinit();
    }

    if (libraryHandle_)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(libraryHandle_));
#else
        dlclose(libraryHandle_);
#endif
    }
}

std::unique_ptr<PluginLibrary> PluginLibrary::load(const std::filesystem::path &path)
{
    // Make sure path is absolute
    std::filesystem::path absolutePath = std::filesystem::absolute(path);
    std::filesystem::path libraryPath = absolutePath;

#ifdef __APPLE__
    // On macOS, .clap files are bundles - we need to find the actual executable inside
    if (absolutePath.extension() == ".clap")
    {
        CFURLRef bundleUrl = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(absolutePath.c_str()),
            absolutePath.string().length(), true);

        if (!bundleUrl)
        {
            throw std::runtime_error("Could not create CFURL for bundle: " + absolutePath.string());
        }

        CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundleUrl);
        CFRelease(bundleUrl);

        if (!bundle)
        {
            throw std::runtime_error("Could not open bundle: " + absolutePath.string());
        }

        CFURLRef executableUrl = CFBundleCopyExecutableURL(bundle);
        CFRelease(bundle);

        if (!executableUrl)
        {
            throw std::runtime_error("Could not get executable URL within bundle: " +
                                     absolutePath.string());
        }

        char executablePath[PATH_MAX];
        if (!CFURLGetFileSystemRepresentation(executableUrl, true,
                                              reinterpret_cast<UInt8 *>(executablePath), PATH_MAX))
        {
            CFRelease(executableUrl);
            throw std::runtime_error("Could not convert bundle executable path");
        }
        CFRelease(executableUrl);

        libraryPath = executablePath;
    }
#endif

    // Load the library
    void *handle = nullptr;

#ifdef _WIN32
    handle = LoadLibraryW(libraryPath.wstring().c_str());
    if (!handle)
    {
        throw std::runtime_error("Could not load plugin library: " + libraryPath.string());
    }
#else
    handle = dlopen(libraryPath.c_str(), RTLD_LOCAL | RTLD_LAZY);
    if (!handle)
    {
        throw std::runtime_error("Could not load plugin library: " + libraryPath.string() + " - " +
                                 dlerror());
    }
#endif

    // Get the entry point
    const clap_plugin_entry_t *entryPoint = nullptr;

#ifdef _WIN32
    entryPoint = reinterpret_cast<const clap_plugin_entry_t *>(
        GetProcAddress(static_cast<HMODULE>(handle), "clap_entry"));
#else
    entryPoint = reinterpret_cast<const clap_plugin_entry_t *>(dlsym(handle, "clap_entry"));
#endif

    if (!entryPoint)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        throw std::runtime_error("The library does not expose a 'clap_entry' symbol: " +
                                 libraryPath.string());
    }

    // Initialize the entry point
    std::string pathStr = absolutePath.string();
    if (!entryPoint->init(pathStr.c_str()))
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        throw std::runtime_error("clap_plugin_entry::init() returned false for: " +
                                 absolutePath.string());
    }

    return std::unique_ptr<PluginLibrary>(new PluginLibrary(absolutePath, handle, entryPoint));
}

PluginLibraryMetadata PluginLibrary::metadata() const
{
    const clap_plugin_factory_t *factory = getPluginFactory();
    if (!factory)
    {
        throw std::runtime_error("The plugin does not support the plugin factory");
    }

    PluginLibraryMetadata metadata;
    metadata.versionMajor = entryPoint_->clap_version.major;
    metadata.versionMinor = entryPoint_->clap_version.minor;
    metadata.versionRevision = entryPoint_->clap_version.revision;

    uint32_t numPlugins = factory->get_plugin_count(factory);
    std::set<std::string> seenIds;

    for (uint32_t i = 0; i < numPlugins; ++i)
    {
        const clap_plugin_descriptor_t *descriptor = factory->get_plugin_descriptor(factory, i);
        if (!descriptor)
        {
            throw std::runtime_error(
                "The plugin returned a null plugin descriptor for plugin index " +
                std::to_string(i));
        }

        PluginMetadata pluginMeta = PluginMetadata::fromDescriptor(descriptor);

        if (seenIds.count(pluginMeta.id))
        {
            throw std::runtime_error(
                "The plugin's factory contains multiple entries for the same plugin ID: " +
                pluginMeta.id);
        }
        seenIds.insert(pluginMeta.id);

        metadata.plugins.push_back(std::move(pluginMeta));
    }

    return metadata;
}

bool PluginLibrary::factoryExists(const std::string &factoryId) const
{
    const void *factory = entryPoint_->get_factory(factoryId.c_str());
    return factory != nullptr;
}

const clap_plugin_factory_t *PluginLibrary::getPluginFactory() const
{
    return reinterpret_cast<const clap_plugin_factory_t *>(
        entryPoint_->get_factory(CLAP_PLUGIN_FACTORY_ID));
}

std::unique_ptr<Plugin> PluginLibrary::createPlugin(const std::string &id,
                                                    std::shared_ptr<Host> host)
{
    const clap_plugin_factory_t *factory = getPluginFactory();
    if (!factory)
    {
        throw std::runtime_error("The plugin does not support the plugin factory");
    }

    return Plugin::create(this, factory, id, std::move(host));
}

} // namespace clap_validator
