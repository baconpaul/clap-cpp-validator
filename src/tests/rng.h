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

#ifndef CLAPVALCPP_SRC_TESTS_RNG_H
#define CLAPVALCPP_SRC_TESTS_RNG_H

#include <cstdint>
#include <random>
#include <vector>
#include "../plugin/process.h"
#include "../plugin/ext.h"

namespace clap_validator
{

// A deterministic pseudo-random number generator. The Rust validator uses a fixed-seed PCG32; we
// use a fixed-seed std::mt19937 (a standard-specified, bit-portable engine) plus our own bounded
// helpers. We deliberately avoid std::uniform_*_distribution because the standard does not pin
// their algorithms, so they diverge across standard libraries and would break the cross-platform
// reproducibility we want. The exact value sequence is not identical to the Rust validator's, but
// it is identical across every platform this validator builds on.
class Prng
{
  public:
    static constexpr uint32_t kDefaultSeed = 1337;

    explicit Prng(uint32_t seed = kDefaultSeed) : engine_(seed) {}

    uint32_t nextU32() { return static_cast<uint32_t>(engine_()); }

    // A uniformly distributed integer in [loInclusive, hiExclusive).
    int nextInt(int loInclusive, int hiExclusive);
    // A uniformly distributed integer in [lo, hi].
    int nextIntInclusive(int lo, int hi) { return nextInt(lo, hi + 1); }
    // A uniformly distributed float in [lo, hi].
    float nextFloat(float lo, float hi);
    // A uniformly distributed double in [lo, hi].
    double nextDouble(double lo, double hi);

  private:
    std::mt19937 engine_;
};

// Create a PRNG with the fixed seed, matching the Rust validator's new_prng() intent.
Prng newPrng();

// A random note and MIDI event generator that produces events consistent with a plugin's
// NotePortConfig. Mirrors the Rust validator's NoteGenerator.
class NoteGenerator
{
  public:
    explicit NoteGenerator(NotePortConfig config);

    // Allow inconsistent events (note-offs without a matching note-on, expressions for notes that
    // aren't playing, and so on).
    NoteGenerator &withInconsistentEvents();

    // Fill the input event queue with random events for the next numSamples. Does not clear the
    // queue; if it was non-empty this stable-sorts by time afterwards. Throws if the plugin's note
    // ports support no note event types.
    void fillEventQueue(Prng &prng, EventList &queue, uint32_t numSamples);

    // Generate a single random note event at the given time offset.
    Event generate(Prng &prng, uint32_t timeOffset);

  private:
    struct Note
    {
        int16_t key;
        int16_t channel;
        int32_t noteId;
        bool choked;

        bool operator==(const Note &o) const
        {
            return key == o.key && channel == o.channel && noteId == o.noteId;
        }
    };

    NotePortConfig config_;
    bool onlyConsistent_ = true;
    std::vector<std::vector<Note>> activeNotes_;
    int32_t nextNoteId_ = 0;
};

// Generates random parameter automation events to stress-test a plugin's parameter handling.
// Mirrors the Rust validator's ParamFuzzer.
class ParamFuzzer
{
  public:
    explicit ParamFuzzer(const ParamInfoMap &config) : config_(&config) {}

    // Append CLAP_EVENT_PARAM_VALUE events randomizing all non-readonly, non-hidden parameters at
    // the given time offset. When nullCookies is true the events carry null cookies.
    void randomizeParamsAt(Prng &prng, uint32_t timeOffset, EventList &queue,
                           bool nullCookies = false) const;

  private:
    const ParamInfoMap *config_;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_TESTS_RNG_H
