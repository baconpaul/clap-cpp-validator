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

#ifndef CLAPVALCPP_SRC_PLUGIN_PROCESS_H
#define CLAPVALCPP_SRC_PLUGIN_PROCESS_H

#include <vector>
#include <cstdint>
#include <clap/clap.h>

namespace clap_validator
{

class Prng;
struct AudioPortConfig;

// A single event sent to or from the plugin. Uses a union over the concrete CLAP event structs
// (they all begin with a clap_event_header) so the queue can hold heterogeneous events without
// allocation. This mirrors the `Event` enum in the Rust validator's process.rs.
struct Event
{
    union
    {
        clap_event_header_t header;
        clap_event_note_t note;
        clap_event_note_expression_t noteExpression;
        clap_event_midi_t midi;
        clap_event_param_value_t paramValue;
        clap_event_param_mod_t paramMod;
    } u{};

    const clap_event_header_t *header() const { return &u.header; }
    clap_event_header_t *header() { return &u.header; }

    static Event fromNote(const clap_event_note_t &e);
    static Event fromNoteExpression(const clap_event_note_expression_t &e);
    static Event fromMidi(const clap_event_midi_t &e);
    static Event fromParamValue(const clap_event_param_value_t &e);
    static Event fromParamMod(const clap_event_param_mod_t &e);

    // Parse an event from a plugin-provided header pointer, copying the correct concrete struct.
    static Event fromHeaderPtr(const clap_event_header_t *ptr);
};

// An event queue usable as both a CLAP input and output event list. Allocated once and never moved
// so the vtable ctx pointers stay stable. Non-copyable and non-movable for the same reason.
class EventList
{
  public:
    EventList();

    EventList(const EventList &) = delete;
    EventList &operator=(const EventList &) = delete;
    EventList(EventList &&) = delete;
    EventList &operator=(EventList &&) = delete;

    void push(const Event &event) { events_.push_back(event); }
    void clear() { events_.clear(); }
    size_t size() const { return events_.size(); }
    const std::vector<Event> &events() const { return events_; }
    std::vector<Event> &events() { return events_; }

    // Stable-time sort, matching the Rust queue's sort_by_key(time) behavior.
    void sortByTime();

    const clap_input_events_t *inputVTable() const { return &inputVTable_; }
    const clap_output_events_t *outputVTable() const { return &outputVTable_; }

  private:
    std::vector<Event> events_;
    clap_input_events_t inputVTable_{};
    clap_output_events_t outputVTable_{};

    static uint32_t CLAP_ABI inputSize(const clap_input_events_t *list);
    static const clap_event_header_t *CLAP_ABI inputGet(const clap_input_events_t *list,
                                                        uint32_t index);
    static bool CLAP_ABI outputTryPush(const clap_output_events_t *list,
                                       const clap_event_header_t *event);
};

// General context information for a process call.
struct ProcessConfig
{
    double sampleRate = 44100.0;
    double tempo = 110.0;
    uint16_t timeSigNumerator = 4;
    uint16_t timeSigDenominator = 4;
};

// Out-of-place audio buffers. Owns the per-port/channel sample storage plus the CLAP pointer
// structures the plugin's process() call needs. Buffers are indexed [port][channel][sample].
// Non-copyable and non-movable because it holds pointers into its own storage.
class AudioBuffers
{
  public:
    AudioBuffers(const AudioPortConfig &config, uint32_t numSamples);

    AudioBuffers(const AudioBuffers &) = delete;
    AudioBuffers &operator=(const AudioBuffers &) = delete;
    AudioBuffers(AudioBuffers &&) = delete;
    AudioBuffers &operator=(AudioBuffers &&) = delete;

    uint32_t numSamples() const { return numSamples_; }

    const std::vector<std::vector<std::vector<float>>> &inputs() const { return inputs_; }
    const std::vector<std::vector<std::vector<float>>> &outputs() const { return outputs_; }

    // A deep copy of the current inputs, used to check the plugin didn't overwrite them.
    std::vector<std::vector<std::vector<float>>> inputsCopy() const { return inputs_; }

    const clap_audio_buffer_t *clapInputs() const { return clapInputs_.data(); }
    clap_audio_buffer_t *clapOutputs() { return clapOutputs_.data(); }
    uint32_t numInputPorts() const { return static_cast<uint32_t>(clapInputs_.size()); }
    uint32_t numOutputPorts() const { return static_cast<uint32_t>(clapOutputs_.size()); }

    // Fill inputs and outputs with white noise in [-1, 1]; subnormals are snapped to zero.
    void randomize(Prng &prng);

  private:
    std::vector<std::vector<std::vector<float>>> inputs_;
    std::vector<std::vector<std::vector<float>>> outputs_;
    std::vector<std::vector<float *>> inputChannelPtrs_;
    std::vector<std::vector<float *>> outputChannelPtrs_;
    std::vector<clap_audio_buffer_t> clapInputs_;
    std::vector<clap_audio_buffer_t> clapOutputs_;
    uint32_t numSamples_;
};

// The input and output data for a call to clap_plugin::process(). Owns the input/output event
// queues and the transport. Non-copyable/non-movable so the clap_process pointers stay valid.
class ProcessData
{
  public:
    ProcessData(AudioBuffers &buffers, const ProcessConfig &config);

    ProcessData(const ProcessData &) = delete;
    ProcessData &operator=(const ProcessData &) = delete;
    ProcessData(ProcessData &&) = delete;
    ProcessData &operator=(ProcessData &&) = delete;

    // Build the clap_process struct pointing at this object's buffers, events, and transport.
    clap_process_t clapProcess();

    void advanceTransport(uint32_t samples);
    void clearEvents();

    EventList &inputEvents() { return inputEvents_; }
    EventList &outputEvents() { return outputEvents_; }

  private:
    AudioBuffers &buffers_;
    EventList inputEvents_;
    EventList outputEvents_;
    ProcessConfig config_;
    clap_event_transport_t transport_{};
    uint32_t samplePos_ = 0;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_PLUGIN_PROCESS_H
