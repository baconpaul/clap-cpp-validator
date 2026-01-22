#pragma once

#include <memory>
#include <string>
#include <clap/clap.h>

namespace clap_validator {

class PluginLibrary;
class Host;

// Plugin status in terms of activation and processing
enum class PluginStatus {
    Inactive,
    ActiveAndSleeping,
    ActiveAndProcessing
};

// A CLAP plugin instance
class Plugin {
public:
    ~Plugin();
    
    // Create a plugin instance from a factory
    static std::unique_ptr<Plugin> create(
        const PluginLibrary* library,
        const clap_plugin_factory_t* factory,
        const std::string& pluginId,
        std::shared_ptr<Host> host
    );
    
    // Initialize the plugin (must be called before activate)
    bool init();
    
    // Activate the plugin for processing
    bool activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount);
    
    // Deactivate the plugin
    void deactivate();
    
    // Start processing
    bool startProcessing();
    
    // Stop processing
    void stopProcessing();
    
    // Process audio
    clap_process_status process(const clap_process_t* processData);
    
    // Get the plugin's descriptor
    const clap_plugin_descriptor_t* descriptor() const;
    
    // Get the raw clap_plugin pointer
    const clap_plugin_t* clapPlugin() const { return plugin_; }
    
    // Get an extension from the plugin
    const void* getExtension(const char* extensionId) const;
    
    // Get the current status
    PluginStatus status() const { return status_; }
    
    // Get the plugin ID
    const std::string& pluginId() const { return pluginId_; }

private:
    Plugin(const clap_plugin_t* plugin, std::shared_ptr<Host> host, const std::string& pluginId);
    
    const clap_plugin_t* plugin_;
    std::shared_ptr<Host> host_;
    std::string pluginId_;
    PluginStatus status_ = PluginStatus::Inactive;
    bool initialized_ = false;
};

} // namespace clap_validator
