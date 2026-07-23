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

#ifndef CLAPVALCPP_SRC_PLUGIN_EXT_H
#define CLAPVALCPP_SRC_PLUGIN_EXT_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <clap/clap.h>

namespace clap_validator
{

class Plugin;
class EventList;

// The configuration for a single audio port. The Rust validator also tracks in-place-pair indices,
// but those are only used for in-place processing, which this validator does not exercise, so we
// keep just the channel count.
struct AudioPort
{
    uint32_t numChannels;
};

// The audio port configuration for a plugin, queried through the 'audio-ports' extension.
struct AudioPortConfig
{
    std::vector<AudioPort> inputs;
    std::vector<AudioPort> outputs;

    // Query the plugin's audio ports. Returns nullopt if the plugin lacks the 'audio-ports'
    // extension. Throws std::runtime_error if the port configuration is inconsistent.
    static std::optional<AudioPortConfig> query(Plugin &plugin);
};

// The configuration for a single note port. Dialects are stored as raw uint32_t bit flags (the
// clap_note_port_info fields are uint32_t, and clap_note_dialect is a plain enum).
struct NotePort
{
    uint32_t preferredDialect;
    // Each supported dialect flag broken out into its own value.
    std::vector<uint32_t> supportedDialects;

    bool supports(uint32_t dialect) const;
};

// The note port configuration for a plugin, queried through the 'note-ports' extension.
struct NotePortConfig
{
    std::vector<NotePort> inputs;
    std::vector<NotePort> outputs;

    // Query the plugin's note ports. Returns nullopt if the plugin lacks the 'note-ports'
    // extension. Throws std::runtime_error if the port configuration is inconsistent.
    static std::optional<NotePortConfig> query(Plugin &plugin);
};

// Information about a single parameter.
struct ParamInfo
{
    clap_id id;
    std::string name;
    std::string module; // path-like grouping, may be empty
    void *cookie;
    double minValue;
    double maxValue;
    double defaultValue;
    clap_param_info_flags flags;

    bool hidden() const;
    bool readonly() const;
    bool stepped() const;
    // Automatable in any form: globally or per note-id/key/channel/port.
    bool automatable() const;
};

// Keyed by stable parameter ID. std::map keeps a consistent iteration order between runs, matching
// the Rust validator's BTreeMap.
using ParamInfoMap = std::map<clap_id, ParamInfo>;

// Main-thread wrapper around the 'params' extension.
class ParamsExt
{
  public:
    // Returns nullopt if the plugin lacks the 'params' extension.
    static std::optional<ParamsExt> create(Plugin &plugin);

    // Query information about all of the plugin's parameters, running the same consistency checks
    // the Rust validator does. Throws std::runtime_error if the parameters are inconsistent.
    ParamInfoMap info() const;

    // Get a parameter's current value. Throws if the plugin returns false.
    double getValue(clap_id paramId) const;

    // Convert a value to its string representation. Returns nullopt if unsupported.
    std::optional<std::string> valueToText(clap_id paramId, double value) const;

    // Convert a string representation to a value. Returns nullopt if unsupported.
    std::optional<double> textToValue(clap_id paramId, const std::string &text) const;

    // Perform a parameter flush. Must be called while the plugin is not active.
    void flush(EventList &inputEvents, EventList &outputEvents) const;

  private:
    ParamsExt(const clap_plugin_t *plugin, const clap_plugin_params_t *params)
        : plugin_(plugin), params_(params)
    {
    }

    const clap_plugin_t *plugin_;
    const clap_plugin_params_t *params_;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_PLUGIN_EXT_H
