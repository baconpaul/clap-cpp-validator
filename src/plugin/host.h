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

#ifndef CLAPVALCPP_SRC_PLUGIN_HOST_H
#define CLAPVALCPP_SRC_PLUGIN_HOST_H

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <clap/clap.h>

namespace clap_validator
{

// Forward declaration
class Plugin;

// An abstraction for a CLAP plugin host used for validation
class Host : public std::enable_shared_from_this<Host>
{
  public:
    Host();
    ~Host();

    // Get the clap_host struct to pass to plugins
    const clap_host_t *clapHost() const { return &clapHost_; }

    // Set the current plugin instance (for callbacks)
    void setCurrentPlugin(Plugin *plugin) { currentPlugin_ = plugin; }

    // Check if any callbacks were called from the wrong thread
    std::optional<std::string> getCallbackError() const;
    void clearCallbackError();

    // Handle pending callbacks
    void handleCallbacksOnce();

    // Thread checking
    bool isMainThread() const;
    void setAudioThread(std::thread::id threadId);
    void clearAudioThread();
    bool isAudioThread() const;

    // Callback flags
    bool hasRequestedCallback() const { return requestedCallback_.load(); }
    void clearRequestedCallback() { requestedCallback_.store(false); }
    bool hasRequestedRestart() const { return requestedRestart_.load(); }
    void clearRequestedRestart() { requestedRestart_.store(false); }

  private:
    // CLAP host callbacks
    static const void *CLAP_ABI getExtension(const clap_host_t *host, const char *extensionId);
    static void CLAP_ABI requestRestart(const clap_host_t *host);
    static void CLAP_ABI requestProcess(const clap_host_t *host);
    static void CLAP_ABI requestCallback(const clap_host_t *host);

    // Thread check extension
    static bool CLAP_ABI isMainThreadExt(const clap_host_t *host);
    static bool CLAP_ABI isAudioThreadExt(const clap_host_t *host);

    // Params extension
    static void CLAP_ABI paramsRescan(const clap_host_t *host, clap_param_rescan_flags flags);
    static void CLAP_ABI paramsClear(const clap_host_t *host, clap_id paramId,
                                     clap_param_clear_flags flags);
    static void CLAP_ABI paramsRequestFlush(const clap_host_t *host);

    // State extension
    static void CLAP_ABI stateMarkDirty(const clap_host_t *host);

    // Helper to get Host from clap_host pointer
    static Host *fromClapHost(const clap_host_t *host);

    // Thread safety assertions
    void assertMainThread(const char *functionName);
    void assertNotAudioThread(const char *functionName);
    void setCallbackError(const std::string &error);

    clap_host_t clapHost_;
    clap_host_thread_check_t threadCheckExt_;
    clap_host_params_t paramsExt_;
    clap_host_state_t stateExt_;

    std::thread::id mainThreadId_;
    std::atomic<std::thread::id> audioThreadId_;

    mutable std::mutex errorMutex_;
    std::optional<std::string> callbackError_;

    Plugin *currentPlugin_ = nullptr;

    std::atomic<bool> requestedCallback_{false};
    std::atomic<bool> requestedRestart_{false};
};

// RAII guard class to mark the current thread as the audio thread
// Usage:
//   {
//       AudioThreadGuard guard(host);
//       plugin->process(...);
//   }
class AudioThreadGuard
{
  public:
    explicit AudioThreadGuard(Host &host) : host_(host)
    {
        host_.setAudioThread(std::this_thread::get_id());
    }

    explicit AudioThreadGuard(std::shared_ptr<Host> host) : host_(*host)
    {
        host_.setAudioThread(std::this_thread::get_id());
    }

    ~AudioThreadGuard() { host_.clearAudioThread(); }

    // Non-copyable, non-movable
    AudioThreadGuard(const AudioThreadGuard &) = delete;
    AudioThreadGuard &operator=(const AudioThreadGuard &) = delete;
    AudioThreadGuard(AudioThreadGuard &&) = delete;
    AudioThreadGuard &operator=(AudioThreadGuard &&) = delete;

  private:
    Host &host_;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_PLUGIN_HOST_H
