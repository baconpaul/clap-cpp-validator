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
#include "host.h"
#include "instance.h"
#include <cstring>

namespace clap_validator
{

Host::Host() : mainThreadId_(std::this_thread::get_id())
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

Host *Host::fromClapHost(const clap_host_t *host)
{
    if (!host || !host->host_data)
    {
        return nullptr;
    }
    return static_cast<Host *>(host->host_data);
}

std::optional<std::string> Host::getCallbackError() const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    return callbackError_;
}

void Host::clearCallbackError()
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    callbackError_.reset();
}

void Host::setCallbackError(const std::string &error)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    if (!callbackError_)
    {
        callbackError_ = error;
    }
}

bool Host::isMainThread() const { return std::this_thread::get_id() == mainThreadId_; }

void Host::setAudioThread(std::thread::id threadId) { audioThreadId_.store(threadId); }

void Host::clearAudioThread() { audioThreadId_.store(std::thread::id{}); }

bool Host::isAudioThread() const
{
    auto audioId = audioThreadId_.load();
    return audioId != std::thread::id{} && std::this_thread::get_id() == audioId;
}

void Host::assertMainThread(const char *functionName)
{
    if (!isMainThread())
    {
        setCallbackError(std::string(functionName) + " must be called from the main thread");
    }
}

void Host::assertNotAudioThread(const char *functionName)
{
    if (isAudioThread())
    {
        setCallbackError(std::string(functionName) + " must not be called from the audio thread");
    }
}

void Host::handleCallbacksOnce()
{
    if (currentPlugin_ && requestedCallback_.exchange(false))
    {
        currentPlugin_->onMainThread();
    }

    // If the plugin asked us to flush its parameters, honor that here. We only do so while the
    // plugin is inactive, because 'clap_plugin_params::flush()' must run on the audio thread while
    // the plugin is active, and this runs on the main thread. The status check short-circuits so
    // the request is left pending (not consumed) until the plugin is inactive.
    if (currentPlugin_ && currentPlugin_->status() == PluginStatus::Inactive &&
        requestedFlush_.exchange(false))
    {
        const auto *paramsExt = static_cast<const clap_plugin_params_t *>(
            currentPlugin_->getExtension(CLAP_EXT_PARAMS));
        if (paramsExt && paramsExt->flush)
        {
            // The host has no parameter changes of its own to send, and we discard any the plugin
            // reports back.
            clap_input_events_t inEvents{};
            inEvents.ctx = nullptr;
            inEvents.size = [](const clap_input_events_t *) -> uint32_t { return 0; };
            inEvents.get = [](const clap_input_events_t *, uint32_t) -> const clap_event_header_t *
            { return nullptr; };

            clap_output_events_t outEvents{};
            outEvents.ctx = nullptr;
            outEvents.try_push = [](const clap_output_events_t *,
                                    const clap_event_header_t *) -> bool { return true; };

            paramsExt->flush(currentPlugin_->clapPlugin(), &inEvents, &outEvents);
        }
    }
}

const void *CLAP_ABI Host::getExtension(const clap_host_t *host, const char *extensionId)
{
    Host *self = fromClapHost(host);
    if (!self || !extensionId)
    {
        return nullptr;
    }

    if (strcmp(extensionId, CLAP_EXT_THREAD_CHECK) == 0)
    {
        return &self->threadCheckExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_PARAMS) == 0)
    {
        return &self->paramsExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_STATE) == 0)
    {
        return &self->stateExt_;
    }

    return nullptr;
}

void CLAP_ABI Host::requestRestart(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->requestedRestart_.store(true);
    }
}

void CLAP_ABI Host::requestProcess(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->requestedProcess_.store(true);
    }
}

void CLAP_ABI Host::requestCallback(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->requestedCallback_.store(true);
    }
}

bool CLAP_ABI Host::isMainThreadExt(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    return self ? self->isMainThread() : false;
}

bool CLAP_ABI Host::isAudioThreadExt(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    return self ? self->isAudioThread() : false;
}

void CLAP_ABI Host::paramsRescan(const clap_host_t *host, clap_param_rescan_flags flags)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_params::rescan()");
    }
    (void)flags;
}

void CLAP_ABI Host::paramsClear(const clap_host_t *host, clap_id paramId,
                                clap_param_clear_flags flags)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_params::clear()");
    }
    (void)paramId;
    (void)flags;
}

void CLAP_ABI Host::paramsRequestFlush(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertNotAudioThread("clap_host_params::request_flush()");
        self->requestedFlush_.store(true);
    }
}

void CLAP_ABI Host::stateMarkDirty(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_state::mark_dirty()");
    }
}

} // namespace clap_validator
