#include "host.h"
#include "instance.h"
#include <cstring>

namespace clap_validator {

Host::Host()
    : mainThreadId_(std::this_thread::get_id())
{
    // Initialize the clap_host struct
    clapHost_.clap_version = CLAP_VERSION;
    clapHost_.host_data = this;
    clapHost_.name = "clap-validator";
    clapHost_.vendor = "CLAP";
    clapHost_.url = "https://github.com/free-audio/clap";
    clapHost_.version = "1.0.0";
    clapHost_.get_extension = &Host::getExtension;
    clapHost_.request_restart = &Host::requestRestart;
    clapHost_.request_process = &Host::requestProcess;
    clapHost_.request_callback = &Host::requestCallback;
    
    // Initialize thread check extension
    threadCheckExt_.is_main_thread = &Host::isMainThreadExt;
    threadCheckExt_.is_audio_thread = &Host::isAudioThreadExt;
    
    // Initialize params extension
    paramsExt_.rescan = &Host::paramsRescan;
    paramsExt_.clear = &Host::paramsClear;
    paramsExt_.request_flush = &Host::paramsRequestFlush;
    
    // Initialize state extension
    stateExt_.mark_dirty = &Host::stateMarkDirty;
}

Host::~Host() = default;

Host* Host::fromClapHost(const clap_host_t* host) {
    if (!host || !host->host_data) {
        return nullptr;
    }
    return static_cast<Host*>(host->host_data);
}

std::optional<std::string> Host::getCallbackError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return callbackError_;
}

void Host::clearCallbackError() {
    std::lock_guard<std::mutex> lock(errorMutex_);
    callbackError_.reset();
}

void Host::setCallbackError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    if (!callbackError_) {
        callbackError_ = error;
    }
}

bool Host::isMainThread() const {
    return std::this_thread::get_id() == mainThreadId_;
}

void Host::setAudioThread(std::thread::id threadId) {
    audioThreadId_.store(threadId);
}

void Host::clearAudioThread() {
    audioThreadId_.store(std::thread::id{});
}

bool Host::isAudioThread() const {
    auto audioId = audioThreadId_.load();
    return audioId != std::thread::id{} && std::this_thread::get_id() == audioId;
}

void Host::assertMainThread(const char* functionName) {
    if (!isMainThread()) {
        setCallbackError(std::string(functionName) + " must be called from the main thread");
    }
}

void Host::assertNotAudioThread(const char* functionName) {
    if (isAudioThread()) {
        setCallbackError(std::string(functionName) + " must not be called from the audio thread");
    }
}

void Host::handleCallbacksOnce() {
    if (currentPlugin_ && requestedCallback_.exchange(false)) {
        // Call on_main_thread on the plugin
        // This would need access to the plugin's clap_plugin pointer
    }
}

const void* CLAP_ABI Host::getExtension(const clap_host_t* host, const char* extensionId) {
    Host* self = fromClapHost(host);
    if (!self || !extensionId) {
        return nullptr;
    }
    
    if (strcmp(extensionId, CLAP_EXT_THREAD_CHECK) == 0) {
        return &self->threadCheckExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_PARAMS) == 0) {
        return &self->paramsExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_STATE) == 0) {
        return &self->stateExt_;
    }
    
    return nullptr;
}

void CLAP_ABI Host::requestRestart(const clap_host_t* host) {
    Host* self = fromClapHost(host);
    if (self) {
        self->requestedRestart_.store(true);
    }
}

void CLAP_ABI Host::requestProcess(const clap_host_t* host) {
    // Not implemented for validator
    (void)host;
}

void CLAP_ABI Host::requestCallback(const clap_host_t* host) {
    Host* self = fromClapHost(host);
    if (self) {
        self->requestedCallback_.store(true);
    }
}

bool CLAP_ABI Host::isMainThreadExt(const clap_host_t* host) {
    Host* self = fromClapHost(host);
    return self ? self->isMainThread() : false;
}

bool CLAP_ABI Host::isAudioThreadExt(const clap_host_t* host) {
    Host* self = fromClapHost(host);
    return self ? self->isAudioThread() : false;
}

void CLAP_ABI Host::paramsRescan(const clap_host_t* host, clap_param_rescan_flags flags) {
    Host* self = fromClapHost(host);
    if (self) {
        self->assertMainThread("clap_host_params::rescan()");
    }
    (void)flags;
}

void CLAP_ABI Host::paramsClear(const clap_host_t* host, clap_id paramId, clap_param_clear_flags flags) {
    Host* self = fromClapHost(host);
    if (self) {
        self->assertMainThread("clap_host_params::clear()");
    }
    (void)paramId;
    (void)flags;
}

void CLAP_ABI Host::paramsRequestFlush(const clap_host_t* host) {
    Host* self = fromClapHost(host);
    if (self) {
        self->assertNotAudioThread("clap_host_params::request_flush()");
    }
}

void CLAP_ABI Host::stateMarkDirty(const clap_host_t* host) {
    Host* self = fromClapHost(host);
    if (self) {
        self->assertMainThread("clap_host_state::mark_dirty()");
    }
}

} // namespace clap_validator
