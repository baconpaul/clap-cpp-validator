#include "instance.h"
#include "host.h"
#include "library.h"
#include <stdexcept>

namespace clap_validator {

Plugin::Plugin(const clap_plugin_t* plugin, std::shared_ptr<Host> host, const std::string& pluginId)
    : plugin_(plugin)
    , host_(std::move(host))
    , pluginId_(pluginId)
{
    host_->setCurrentPlugin(this);
}

Plugin::~Plugin() {
    if (status_ == PluginStatus::ActiveAndProcessing) {
        stopProcessing();
    }
    if (status_ == PluginStatus::ActiveAndSleeping) {
        deactivate();
    }
    
    if (plugin_) {
        if (initialized_) {
            plugin_->destroy(plugin_);
        }
    }
    
    if (host_) {
        host_->setCurrentPlugin(nullptr);
    }
}

std::unique_ptr<Plugin> Plugin::create(
    const PluginLibrary* library,
    const clap_plugin_factory_t* factory,
    const std::string& pluginId,
    std::shared_ptr<Host> host)
{
    if (!factory || !host) {
        throw std::runtime_error("Invalid factory or host");
    }
    
    const clap_plugin_t* plugin = factory->create_plugin(
        factory,
        host->clapHost(),
        pluginId.c_str()
    );
    
    if (!plugin) {
        throw std::runtime_error("Failed to create plugin: " + pluginId);
    }
    
    return std::unique_ptr<Plugin>(new Plugin(plugin, std::move(host), pluginId));
}

bool Plugin::init() {
    if (initialized_) {
        return true;
    }
    
    if (!plugin_ || !plugin_->init) {
        return false;
    }
    
    initialized_ = plugin_->init(plugin_);
    return initialized_;
}

bool Plugin::activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) {
    if (!initialized_) {
        return false;
    }
    
    if (status_ != PluginStatus::Inactive) {
        return false;
    }
    
    if (!plugin_ || !plugin_->activate) {
        return false;
    }
    
    if (plugin_->activate(plugin_, sampleRate, minFrameCount, maxFrameCount)) {
        status_ = PluginStatus::ActiveAndSleeping;
        return true;
    }
    
    return false;
}

void Plugin::deactivate() {
    if (status_ == PluginStatus::ActiveAndProcessing) {
        stopProcessing();
    }
    
    if (status_ != PluginStatus::ActiveAndSleeping) {
        return;
    }
    
    if (plugin_ && plugin_->deactivate) {
        plugin_->deactivate(plugin_);
    }
    
    status_ = PluginStatus::Inactive;
}

bool Plugin::startProcessing() {
    if (status_ != PluginStatus::ActiveAndSleeping) {
        return false;
    }
    
    if (!plugin_ || !plugin_->start_processing) {
        // start_processing is optional
        status_ = PluginStatus::ActiveAndProcessing;
        return true;
    }
    
    if (plugin_->start_processing(plugin_)) {
        status_ = PluginStatus::ActiveAndProcessing;
        return true;
    }
    
    return false;
}

void Plugin::stopProcessing() {
    if (status_ != PluginStatus::ActiveAndProcessing) {
        return;
    }
    
    if (plugin_ && plugin_->stop_processing) {
        plugin_->stop_processing(plugin_);
    }
    
    status_ = PluginStatus::ActiveAndSleeping;
}

clap_process_status Plugin::process(const clap_process_t* processData) {
    if (status_ != PluginStatus::ActiveAndProcessing) {
        return CLAP_PROCESS_ERROR;
    }
    
    if (!plugin_ || !plugin_->process) {
        return CLAP_PROCESS_ERROR;
    }
    
    return plugin_->process(plugin_, processData);
}

const clap_plugin_descriptor_t* Plugin::descriptor() const {
    return plugin_ ? plugin_->desc : nullptr;
}

const void* Plugin::getExtension(const char* extensionId) const {
    if (!plugin_ || !plugin_->get_extension || !extensionId) {
        return nullptr;
    }
    
    return plugin_->get_extension(plugin_, extensionId);
}

} // namespace clap_validator
