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
#include "process.h"
#include "ext.h"
#include "../tests/rng.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace clap_validator
{

Event Event::fromNote(const clap_event_note_t &e)
{
    Event ev;
    ev.u.note = e;
    return ev;
}

Event Event::fromNoteExpression(const clap_event_note_expression_t &e)
{
    Event ev;
    ev.u.noteExpression = e;
    return ev;
}

Event Event::fromMidi(const clap_event_midi_t &e)
{
    Event ev;
    ev.u.midi = e;
    return ev;
}

Event Event::fromParamValue(const clap_event_param_value_t &e)
{
    Event ev;
    ev.u.paramValue = e;
    return ev;
}

Event Event::fromParamMod(const clap_event_param_mod_t &e)
{
    Event ev;
    ev.u.paramMod = e;
    return ev;
}

Event Event::fromHeaderPtr(const clap_event_header_t *ptr)
{
    Event ev;
    if (!ptr)
    {
        return ev;
    }

    if (ptr->space_id == CLAP_CORE_EVENT_SPACE_ID)
    {
        switch (ptr->type)
        {
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
        case CLAP_EVENT_NOTE_END:
            ev.u.note = *reinterpret_cast<const clap_event_note_t *>(ptr);
            return ev;
        case CLAP_EVENT_NOTE_EXPRESSION:
            ev.u.noteExpression = *reinterpret_cast<const clap_event_note_expression_t *>(ptr);
            return ev;
        case CLAP_EVENT_PARAM_VALUE:
            ev.u.paramValue = *reinterpret_cast<const clap_event_param_value_t *>(ptr);
            return ev;
        case CLAP_EVENT_PARAM_MOD:
            ev.u.paramMod = *reinterpret_cast<const clap_event_param_mod_t *>(ptr);
            return ev;
        case CLAP_EVENT_MIDI:
            ev.u.midi = *reinterpret_cast<const clap_event_midi_t *>(ptr);
            return ev;
        default:
            break;
        }
    }

    // Unknown/unhandled event: keep just the header.
    ev.u.header = *ptr;
    return ev;
}

EventList::EventList()
{
    inputVTable_.ctx = this;
    inputVTable_.size = &EventList::inputSize;
    inputVTable_.get = &EventList::inputGet;

    outputVTable_.ctx = this;
    outputVTable_.try_push = &EventList::outputTryPush;
}

void EventList::sortByTime()
{
    std::stable_sort(events_.begin(), events_.end(), [](const Event &a, const Event &b)
                     { return a.header()->time < b.header()->time; });
}

uint32_t CLAP_ABI EventList::inputSize(const clap_input_events_t *list)
{
    auto *self = static_cast<EventList *>(list->ctx);
    return static_cast<uint32_t>(self->events_.size());
}

const clap_event_header_t *CLAP_ABI EventList::inputGet(const clap_input_events_t *list,
                                                        uint32_t index)
{
    auto *self = static_cast<EventList *>(list->ctx);
    if (index >= self->events_.size())
    {
        return nullptr;
    }
    return self->events_[index].header();
}

bool CLAP_ABI EventList::outputTryPush(const clap_output_events_t *list,
                                       const clap_event_header_t *event)
{
    if (!event)
    {
        return false;
    }
    auto *self = static_cast<EventList *>(list->ctx);
    self->events_.push_back(Event::fromHeaderPtr(event));
    return true;
}

AudioBuffers::AudioBuffers(const AudioPortConfig &config, uint32_t numSamples)
    : numSamples_(numSamples)
{
    auto allocPorts = [numSamples](const std::vector<AudioPort> &ports)
    {
        std::vector<std::vector<std::vector<float>>> result;
        result.reserve(ports.size());
        for (const auto &port : ports)
        {
            result.emplace_back(port.numChannels, std::vector<float>(numSamples, 0.0f));
        }
        return result;
    };

    inputs_ = allocPorts(config.inputs);
    outputs_ = allocPorts(config.outputs);

    auto buildBuffers = [](std::vector<std::vector<std::vector<float>>> &ports,
                           std::vector<std::vector<float *>> &channelPtrs,
                           std::vector<clap_audio_buffer_t> &clapBuffers)
    {
        channelPtrs.resize(ports.size());
        clapBuffers.resize(ports.size());
        for (size_t p = 0; p < ports.size(); ++p)
        {
            channelPtrs[p].resize(ports[p].size());
            for (size_t c = 0; c < ports[p].size(); ++c)
            {
                channelPtrs[p][c] = ports[p][c].data();
            }

            clapBuffers[p] = {};
            clapBuffers[p].data32 = channelPtrs[p].data();
            clapBuffers[p].data64 = nullptr;
            clapBuffers[p].channel_count = static_cast<uint32_t>(ports[p].size());
            clapBuffers[p].latency = 0;
            clapBuffers[p].constant_mask = 0;
        }
    };

    buildBuffers(inputs_, inputChannelPtrs_, clapInputs_);
    buildBuffers(outputs_, outputChannelPtrs_, clapOutputs_);
}

void AudioBuffers::randomize(Prng &prng)
{
    auto fill = [&prng](std::vector<std::vector<std::vector<float>>> &ports)
    {
        for (auto &channels : ports)
        {
            for (auto &channel : channels)
            {
                for (auto &sample : channel)
                {
                    sample = prng.nextFloat(-1.0f, 1.0f);
                    if (std::fpclassify(sample) == FP_SUBNORMAL)
                    {
                        sample = 0.0f;
                    }
                }
            }
        }
    };

    fill(inputs_);
    fill(outputs_);
}

ProcessData::ProcessData(AudioBuffers &buffers, const ProcessConfig &config)
    : buffers_(buffers), config_(config)
{
    transport_.header.size = sizeof(clap_event_transport_t);
    transport_.header.time = 0;
    transport_.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport_.header.type = CLAP_EVENT_TRANSPORT;
    transport_.header.flags = 0;
    transport_.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                       CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE |
                       CLAP_TRANSPORT_IS_PLAYING;
    transport_.tempo = config.tempo;
    transport_.tsig_num = config.timeSigNumerator;
    transport_.tsig_denom = config.timeSigDenominator;
}

clap_process_t ProcessData::clapProcess()
{
    clap_process_t process = {};
    process.steady_time = static_cast<int64_t>(samplePos_);
    process.frames_count = buffers_.numSamples();
    process.transport = &transport_;
    process.audio_inputs = buffers_.numInputPorts() > 0 ? buffers_.clapInputs() : nullptr;
    process.audio_outputs = buffers_.numOutputPorts() > 0 ? buffers_.clapOutputs() : nullptr;
    process.audio_inputs_count = buffers_.numInputPorts();
    process.audio_outputs_count = buffers_.numOutputPorts();
    process.in_events = inputEvents_.inputVTable();
    process.out_events = outputEvents_.outputVTable();
    return process;
}

void ProcessData::advanceTransport(uint32_t samples)
{
    samplePos_ += samples;

    const double seconds = static_cast<double>(samplePos_) / config_.sampleRate;
    transport_.song_pos_beats = static_cast<clap_beattime>(
        std::llround((seconds / 60.0 * transport_.tempo) * CLAP_BEATTIME_FACTOR));
    transport_.song_pos_seconds =
        static_cast<clap_sectime>(std::llround(seconds * CLAP_SECTIME_FACTOR));
}

void ProcessData::clearEvents()
{
    inputEvents_.clear();
    outputEvents_.clear();
}

} // namespace clap_validator
