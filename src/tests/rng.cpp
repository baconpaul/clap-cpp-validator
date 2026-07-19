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
#include "rng.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace clap_validator
{

namespace
{
// MIDI status nibbles (channel is OR'd into the low nibble).
constexpr uint8_t MIDI_NOTE_OFF = 0x80;
constexpr uint8_t MIDI_NOTE_ON = 0x90;
constexpr uint8_t MIDI_POLY_KEY_PRESSURE = 0xA0;
constexpr uint8_t MIDI_CONTROL_CHANGE = 0xB0;
constexpr uint8_t MIDI_PROGRAM_CHANGE = 0xC0;
constexpr uint8_t MIDI_CHANNEL_PRESSURE = 0xD0;
constexpr uint8_t MIDI_PITCH_BEND = 0xE0;

enum class NoteEventType
{
    ClapNoteOn,
    ClapNoteOff,
    ClapNoteChoke,
    ClapNoteExpression,
    MidiNoteOn,
    MidiNoteOff,
    MidiChannelPressure,
    MidiPolyKeyPressure,
    MidiPitchBend,
    MidiCc,
    MidiProgramChange,
};

std::vector<NoteEventType> supportedTypes(bool supportsClap, bool supportsMidi)
{
    std::vector<NoteEventType> clapEvents = {NoteEventType::ClapNoteOn, NoteEventType::ClapNoteOff,
                                             NoteEventType::ClapNoteChoke,
                                             NoteEventType::ClapNoteExpression};
    std::vector<NoteEventType> midiEvents = {
        NoteEventType::MidiNoteOn,          NoteEventType::MidiNoteOff,
        NoteEventType::MidiChannelPressure, NoteEventType::MidiPolyKeyPressure,
        NoteEventType::MidiPitchBend,       NoteEventType::MidiCc,
        NoteEventType::MidiProgramChange};

    std::vector<NoteEventType> result;
    if (supportsClap)
    {
        result.insert(result.end(), clapEvents.begin(), clapEvents.end());
    }
    if (supportsMidi)
    {
        result.insert(result.end(), midiEvents.begin(), midiEvents.end());
    }
    return result;
}

uint8_t velocityToMidi(float velocity)
{
    return static_cast<uint8_t>(std::clamp(std::lround(velocity * 127.0f), 0L, 127L));
}
} // namespace

int Prng::nextInt(int loInclusive, int hiExclusive)
{
    if (hiExclusive <= loInclusive)
    {
        return loInclusive;
    }
    uint32_t span = static_cast<uint32_t>(hiExclusive - loInclusive);
    return loInclusive + static_cast<int>(nextU32() % span);
}

float Prng::nextFloat(float lo, float hi)
{
    double t = static_cast<double>(nextU32()) / static_cast<double>(0xFFFFFFFFu);
    return static_cast<float>(lo + t * (static_cast<double>(hi) - static_cast<double>(lo)));
}

double Prng::nextDouble(double lo, double hi)
{
    double t = static_cast<double>(nextU32()) / static_cast<double>(0xFFFFFFFFu);
    return lo + t * (hi - lo);
}

Prng newPrng() { return Prng(Prng::kDefaultSeed); }

NoteGenerator::NoteGenerator(NotePortConfig config) : config_(std::move(config))
{
    activeNotes_.resize(config_.inputs.size());
}

NoteGenerator &NoteGenerator::withInconsistentEvents()
{
    onlyConsistent_ = false;
    return *this;
}

void NoteGenerator::fillEventQueue(Prng &prng, EventList &queue, uint32_t numSamples)
{
    // The next event's time offset relative to the current sample, capped at 0 (so ~58% of the
    // time the next event lands on the same sample as the previous one).
    bool shouldSort = queue.size() != 0;

    uint32_t currentSample = static_cast<uint32_t>(std::max(0, prng.nextIntInclusive(-6, 5)));
    while (currentSample < numSamples)
    {
        queue.push(generate(prng, currentSample));
        currentSample += static_cast<uint32_t>(std::max(0, prng.nextIntInclusive(-6, 5)));
    }

    if (shouldSort)
    {
        queue.sortByTime();
    }
}

Event NoteGenerator::generate(Prng &prng, uint32_t timeOffset)
{
    if (config_.inputs.empty())
    {
        throw std::runtime_error(
            "Cannot generate note events for a plugin with no input note ports.");
    }

    int notePortIdx = prng.nextInt(0, static_cast<int>(config_.inputs.size()));
    const NotePort &port = config_.inputs[notePortIdx];
    bool supportsClap = port.supports(CLAP_NOTE_DIALECT_CLAP);
    bool supportsMidi =
        port.supports(CLAP_NOTE_DIALECT_MIDI) || port.supports(CLAP_NOTE_DIALECT_MIDI_MPE);

    std::vector<NoteEventType> possible = supportedTypes(supportsClap, supportsMidi);
    if (possible.empty())
    {
        throw std::runtime_error("Note input port " + std::to_string(notePortIdx) +
                                 " supports neither CLAP note events nor MIDI.");
    }

    auto &active = activeNotes_[notePortIdx];

    auto makeNoteEvent = [&](uint16_t eventType, const Note &note, float velocity)
    {
        clap_event_note_t e = {};
        e.header.size = sizeof(clap_event_note_t);
        e.header.time = timeOffset;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = eventType;
        e.header.flags = 0;
        e.note_id = note.noteId;
        e.port_index = static_cast<int16_t>(notePortIdx);
        e.channel = note.channel;
        e.key = note.key;
        e.velocity = velocity;
        return Event::fromNote(e);
    };

    auto makeMidiEvent = [&](uint8_t b0, uint8_t b1, uint8_t b2)
    {
        clap_event_midi_t e = {};
        e.header.size = sizeof(clap_event_midi_t);
        e.header.time = timeOffset;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_MIDI;
        e.header.flags = 0;
        e.port_index = static_cast<uint16_t>(notePortIdx);
        e.data[0] = b0;
        e.data[1] = b1;
        e.data[2] = b2;
        return Event::fromMidi(e);
    };

    auto randomNote = [&]() -> Note
    {
        return Note{static_cast<int16_t>(prng.nextInt(0, 128)),
                    static_cast<int16_t>(prng.nextInt(0, 16)), prng.nextInt(0, 100), false};
    };

    for (int attempt = 0; attempt < 1024; ++attempt)
    {
        NoteEventType type = possible[prng.nextInt(0, static_cast<int>(possible.size()))];
        switch (type)
        {
        case NoteEventType::ClapNoteOn:
        case NoteEventType::MidiNoteOn:
        {
            Note note;
            if (onlyConsistent_)
            {
                note = Note{static_cast<int16_t>(prng.nextInt(0, 128)),
                            static_cast<int16_t>(prng.nextInt(0, 16)), nextNoteId_, false};
                if (std::find(active.begin(), active.end(), note) != active.end())
                {
                    continue;
                }
                active.push_back(note);
                nextNoteId_++;
            }
            else
            {
                note = randomNote();
            }
            float velocity = prng.nextFloat(0.0f, 1.0f);
            if (type == NoteEventType::ClapNoteOn)
            {
                return makeNoteEvent(CLAP_EVENT_NOTE_ON, note, velocity);
            }
            return makeMidiEvent(MIDI_NOTE_ON | static_cast<uint8_t>(note.channel),
                                 static_cast<uint8_t>(note.key), velocityToMidi(velocity));
        }
        case NoteEventType::ClapNoteOff:
        case NoteEventType::MidiNoteOff:
        {
            Note note;
            if (onlyConsistent_)
            {
                if (active.empty())
                {
                    continue;
                }
                int idx = prng.nextInt(0, static_cast<int>(active.size()));
                note = active[idx];
                active.erase(active.begin() + idx);
            }
            else
            {
                note = randomNote();
            }
            float velocity = prng.nextFloat(0.0f, 1.0f);
            if (type == NoteEventType::ClapNoteOff)
            {
                return makeNoteEvent(CLAP_EVENT_NOTE_OFF, note, velocity);
            }
            return makeMidiEvent(MIDI_NOTE_OFF | static_cast<uint8_t>(note.channel),
                                 static_cast<uint8_t>(note.key), velocityToMidi(velocity));
        }
        case NoteEventType::ClapNoteChoke:
        {
            Note note;
            if (onlyConsistent_)
            {
                if (active.empty())
                {
                    continue;
                }
                int idx = prng.nextInt(0, static_cast<int>(active.size()));
                if (active[idx].choked)
                {
                    continue;
                }
                active[idx].choked = true;
                note = active[idx];
            }
            else
            {
                note = randomNote();
            }
            float velocity = prng.nextFloat(0.0f, 1.0f);
            return makeNoteEvent(CLAP_EVENT_NOTE_CHOKE, note, velocity);
        }
        case NoteEventType::ClapNoteExpression:
        {
            Note note;
            if (onlyConsistent_)
            {
                if (active.empty())
                {
                    continue;
                }
                int idx = prng.nextInt(0, static_cast<int>(active.size()));
                note = active[idx];
            }
            else
            {
                note = randomNote();
            }

            int expressionId =
                prng.nextIntInclusive(CLAP_NOTE_EXPRESSION_VOLUME, CLAP_NOTE_EXPRESSION_PRESSURE);
            double lo = 0.0, hi = 1.0;
            if (expressionId == CLAP_NOTE_EXPRESSION_VOLUME)
            {
                lo = 0.0;
                hi = 4.0;
            }
            else if (expressionId == CLAP_NOTE_EXPRESSION_TUNING)
            {
                lo = -128.0;
                hi = 128.0;
            }
            double value = prng.nextDouble(lo, hi);

            clap_event_note_expression_t e = {};
            e.header.size = sizeof(clap_event_note_expression_t);
            e.header.time = timeOffset;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            // NOTE: the Rust validator set this to CLAP_EVENT_NOTE_CHOKE, which was a copy-paste
            // bug; a note-expression event must be typed CLAP_EVENT_NOTE_EXPRESSION.
            e.header.type = CLAP_EVENT_NOTE_EXPRESSION;
            e.header.flags = 0;
            e.expression_id = expressionId;
            e.note_id = note.noteId;
            e.port_index = static_cast<int16_t>(notePortIdx);
            e.channel = note.channel;
            e.key = note.key;
            e.value = value;
            return Event::fromNoteExpression(e);
        }
        case NoteEventType::MidiChannelPressure:
        {
            uint8_t channel = static_cast<uint8_t>(prng.nextInt(0, 16));
            uint8_t pressure = static_cast<uint8_t>(prng.nextInt(0, 128));
            return makeMidiEvent(MIDI_CHANNEL_PRESSURE | channel, pressure, 0);
        }
        case NoteEventType::MidiPolyKeyPressure:
        {
            Note note;
            if (onlyConsistent_)
            {
                if (active.empty())
                {
                    continue;
                }
                int idx = prng.nextInt(0, static_cast<int>(active.size()));
                note = active[idx];
            }
            else
            {
                note = randomNote();
            }
            uint8_t pressure = static_cast<uint8_t>(prng.nextInt(0, 128));
            return makeMidiEvent(MIDI_POLY_KEY_PRESSURE | static_cast<uint8_t>(note.channel),
                                 static_cast<uint8_t>(note.key), pressure);
        }
        case NoteEventType::MidiPitchBend:
        {
            uint8_t channel = static_cast<uint8_t>(prng.nextInt(0, 16));
            uint8_t byte1 = static_cast<uint8_t>(prng.nextInt(0, 128));
            uint8_t byte2 = static_cast<uint8_t>(prng.nextInt(0, 128));
            return makeMidiEvent(MIDI_PITCH_BEND | channel, byte1, byte2);
        }
        case NoteEventType::MidiCc:
        {
            uint8_t channel = static_cast<uint8_t>(prng.nextInt(0, 16));
            uint8_t cc = static_cast<uint8_t>(prng.nextInt(0, 128));
            uint8_t value = static_cast<uint8_t>(prng.nextInt(0, 128));
            return makeMidiEvent(MIDI_CONTROL_CHANGE | channel, cc, value);
        }
        case NoteEventType::MidiProgramChange:
        {
            uint8_t channel = static_cast<uint8_t>(prng.nextInt(0, 16));
            uint8_t program = static_cast<uint8_t>(prng.nextInt(0, 128));
            return makeMidiEvent(MIDI_PROGRAM_CHANGE | channel, program, 0);
        }
        }
    }

    throw std::runtime_error(
        "Unable to generate a random note event after 1024 tries, this is a bug in the validator.");
}

void ParamFuzzer::randomizeParamsAt(Prng &prng, uint32_t timeOffset, EventList &queue,
                                    bool nullCookies) const
{
    for (const auto &[id, info] : *config_)
    {
        if (info.readonly() || info.hidden())
        {
            continue;
        }

        double value = info.stepped() ? std::round(prng.nextDouble(info.minValue, info.maxValue))
                                      : prng.nextDouble(info.minValue, info.maxValue);

        clap_event_param_value_t e = {};
        e.header.size = sizeof(clap_event_param_value_t);
        e.header.time = timeOffset;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.header.flags = 0;
        e.param_id = id;
        e.cookie = nullCookies ? nullptr : info.cookie;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = value;
        queue.push(Event::fromParamValue(e));
    }
}

} // namespace clap_validator
