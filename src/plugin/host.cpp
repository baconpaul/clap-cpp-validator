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
#include <iostream>

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

    // Initialize log extension
    logExt_.log = &Host::logMessage;

    // Initialize audio-ports host extension
    audioPortsExt_.is_rescan_flag_supported = &Host::audioPortsIsRescanFlagSupported;
    audioPortsExt_.rescan = &Host::audioPortsRescan;

    // Initialize note-ports host extension
    notePortsExt_.supported_dialects = &Host::notePortsSupportedDialects;
    notePortsExt_.rescan = &Host::notePortsRescan;

    // Initialize the "changed" notification extensions
    latencyExt_.changed = &Host::latencyChanged;
    tailExt_.changed = &Host::tailChanged;
    noteNameExt_.changed = &Host::noteNameChanged;
    voiceInfoExt_.changed = &Host::voiceInfoChanged;

    // Initialize preset-load host extension
    presetLoadExt_.on_error = &Host::presetLoadOnError;
    presetLoadExt_.loaded = &Host::presetLoadLoaded;
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
    if (strcmp(extensionId, CLAP_EXT_LOG) == 0)
    {
        return &self->logExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_AUDIO_PORTS) == 0)
    {
        return &self->audioPortsExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_NOTE_PORTS) == 0)
    {
        return &self->notePortsExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_LATENCY) == 0)
    {
        return &self->latencyExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_TAIL) == 0)
    {
        return &self->tailExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_NOTE_NAME) == 0)
    {
        return &self->noteNameExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_VOICE_INFO) == 0)
    {
        return &self->voiceInfoExt_;
    }
    if (strcmp(extensionId, CLAP_EXT_PRESET_LOAD) == 0 ||
        strcmp(extensionId, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
    {
        return &self->presetLoadExt_;
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

void CLAP_ABI Host::logMessage(const clap_host_t *host, clap_log_severity severity, const char *msg)
{
    Host *self = fromClapHost(host);
    if (!self)
    {
        return;
    }

    const char *level = "UNKNOWN";
    switch (severity)
    {
    case CLAP_LOG_DEBUG:
        level = "DEBUG";
        break;
    case CLAP_LOG_INFO:
        level = "INFO";
        break;
    case CLAP_LOG_WARNING:
        level = "WARNING";
        break;
    case CLAP_LOG_ERROR:
        level = "ERROR";
        break;
    case CLAP_LOG_FATAL:
        level = "FATAL";
        break;
    case CLAP_LOG_HOST_MISBEHAVING:
        level = "HOST_MISBEHAVING";
        break;
    case CLAP_LOG_PLUGIN_MISBEHAVING:
        level = "PLUGIN_MISBEHAVING";
        break;
    default:
        break;
    }

    const std::string message = msg ? msg : "<null>";

    // Print WARNING and above so the plugin's log surfaces (subject to --show-plugin-stdout). This
    // callback is [thread-safe] and may run on the audio thread; a validator can afford stderr.
    if (severity >= CLAP_LOG_WARNING)
    {
        std::lock_guard<std::mutex> lock(self->logMutex_);
        std::cerr << "[clap-log:" << level << "] " << message << "\n";
    }

    // PLUGIN_MISBEHAVING is the plugin (or a layer) self-reporting the plugin's own fault, so we
    // surface it as a finding via the callback-error path the checks already inspect.
    // HOST_MISBEHAVING points at the host (us) - it is printed above but not treated as a plugin
    // failure, since it usually means the validator host should change rather than the plugin.
    if (severity == CLAP_LOG_PLUGIN_MISBEHAVING)
    {
        self->setCallbackError(std::string("The plugin logged a ") + level +
                               " message via clap_host_log: " + message);
    }
}

bool CLAP_ABI Host::audioPortsIsRescanFlagSupported(const clap_host_t *host, uint32_t)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_audio_ports::is_rescan_flag_supported()");
    }
    // We accept a rescan of any aspect of the audio ports.
    return true;
}

void CLAP_ABI Host::audioPortsRescan(const clap_host_t *host, uint32_t)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_audio_ports::rescan()");
    }
}

uint32_t CLAP_ABI Host::notePortsSupportedDialects(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_note_ports::supported_dialects()");
    }
    // The dialects our note generator can actually produce (MIDI2 is intentionally excluded).
    return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
}

void CLAP_ABI Host::notePortsRescan(const clap_host_t *host, uint32_t)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_note_ports::rescan()");
    }
}

void CLAP_ABI Host::latencyChanged(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_latency::changed()");
    }
}

void CLAP_ABI Host::tailChanged(const clap_host_t *host)
{
    // clap_host_tail::changed() is documented [audio-thread]; we simply accept it.
    (void)host;
}

void CLAP_ABI Host::noteNameChanged(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_note_name::changed()");
    }
}

void CLAP_ABI Host::voiceInfoChanged(const clap_host_t *host)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_voice_info::changed()");
    }
}

void CLAP_ABI Host::presetLoadOnError(const clap_host_t *host, uint32_t /*locationKind*/,
                                      const char *location, const char *loadKey, int32_t osError,
                                      const char *msg)
{
    Host *self = fromClapHost(host);
    if (!self)
    {
        return;
    }
    self->assertMainThread("clap_host_preset_load::on_error()");

    std::string message = "The plugin reported a preset load error";
    if (msg)
    {
        message += ": ";
        message += msg;
    }
    if (location)
    {
        message += " (location: ";
        message += location;
        message += ")";
    }
    if (loadKey)
    {
        message += " (load key: ";
        message += loadKey;
        message += ")";
    }
    message += " [os error " + std::to_string(osError) + "]";
    self->setCallbackError(message);
}

void CLAP_ABI Host::presetLoadLoaded(const clap_host_t *host, uint32_t /*locationKind*/,
                                     const char * /*location*/, const char * /*loadKey*/)
{
    Host *self = fromClapHost(host);
    if (self)
    {
        self->assertMainThread("clap_host_preset_load::loaded()");
    }
}

} // namespace clap_validator
