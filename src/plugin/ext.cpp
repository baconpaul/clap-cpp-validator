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
#include "ext.h"
#include "instance.h"
#include "process.h"
#include <bit>
#include <cstring>
#include <stdexcept>

namespace clap_validator
{

namespace
{
// Read a fixed-size, null-terminated CLAP string buffer into a std::string.
std::string fixedToString(const char *buf, size_t maxLen)
{
    return std::string(buf, ::strnlen(buf, maxLen));
}

[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }
} // namespace

bool NotePort::supports(uint32_t dialect) const
{
    for (auto d : supportedDialects)
    {
        if (d == dialect)
        {
            return true;
        }
    }
    return false;
}

std::optional<AudioPortConfig> AudioPortConfig::query(Plugin &plugin)
{
    const auto *ext =
        static_cast<const clap_plugin_audio_ports_t *>(plugin.getExtension(CLAP_EXT_AUDIO_PORTS));
    if (!ext)
    {
        return std::nullopt;
    }

    const clap_plugin_t *cp = plugin.clapPlugin();
    AudioPortConfig config;

    auto typeConsistent = [](const clap_audio_port_info_t &info)
    {
        if (info.port_type == nullptr)
        {
            return;
        }
        if (std::strcmp(info.port_type, CLAP_PORT_MONO) == 0 && info.channel_count != 1)
        {
            fail("Expected 1 channel for a mono audio port, but it has " +
                 std::to_string(info.channel_count) + ".");
        }
        if (std::strcmp(info.port_type, CLAP_PORT_STEREO) == 0 && info.channel_count != 2)
        {
            fail("Expected 2 channels for a stereo audio port, but it has " +
                 std::to_string(info.channel_count) + ".");
        }
    };

    auto queryPorts = [&](bool isInput, std::vector<AudioPort> &out)
    {
        uint32_t count = ext->count(cp, isInput);
        std::vector<uint32_t> seenIds;
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_audio_port_info_t info = {};
            if (!ext->get(cp, i, isInput, &info))
            {
                fail("Plugin returned an error when querying audio port " + std::to_string(i) +
                     ".");
            }
            typeConsistent(info);
            for (uint32_t seen : seenIds)
            {
                if (seen == info.id)
                {
                    fail("The stable ID of audio port " + std::to_string(i) + " is a duplicate.");
                }
            }
            seenIds.push_back(info.id);
            out.push_back(AudioPort{info.channel_count});
        }
    };

    queryPorts(true, config.inputs);
    queryPorts(false, config.outputs);
    return config;
}

std::optional<NotePortConfig> NotePortConfig::query(Plugin &plugin)
{
    const auto *ext =
        static_cast<const clap_plugin_note_ports_t *>(plugin.getExtension(CLAP_EXT_NOTE_PORTS));
    if (!ext)
    {
        return std::nullopt;
    }

    const clap_plugin_t *cp = plugin.clapPlugin();
    NotePortConfig config;

    auto queryPorts = [&](bool isInput, std::vector<NotePort> &out)
    {
        uint32_t count = ext->count(cp, isInput);
        std::vector<uint32_t> seenIds;
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_note_port_info_t info = {};
            // NOTE: the Rust validator queried output ports with is_input=true here; that was a
            // copy-paste bug. We pass the correct `isInput`.
            if (!ext->get(cp, i, isInput, &info))
            {
                fail("Plugin returned an error when querying note port " + std::to_string(i) + ".");
            }

            if (std::popcount(static_cast<unsigned>(info.preferred_dialect)) != 1)
            {
                fail("Note port " + std::to_string(i) +
                     " does not prefer exactly one note dialect.");
            }
            if ((info.supported_dialects & info.preferred_dialect) == 0)
            {
                fail("Note port " + std::to_string(i) +
                     " prefers a dialect that is not in its supported dialects.");
            }
            for (uint32_t seen : seenIds)
            {
                if (seen == info.id)
                {
                    fail("The stable ID of note port " + std::to_string(i) + " is a duplicate.");
                }
            }
            seenIds.push_back(info.id);

            NotePort port;
            port.preferredDialect = info.preferred_dialect;
            for (uint32_t bit = 0; bit < sizeof(uint32_t) * 8; ++bit)
            {
                uint32_t flag = 1u << bit;
                if ((info.supported_dialects & flag) != 0)
                {
                    port.supportedDialects.push_back(flag);
                }
            }
            out.push_back(std::move(port));
        }
    };

    queryPorts(true, config.inputs);
    queryPorts(false, config.outputs);
    return config;
}

bool ParamInfo::hidden() const { return (flags & CLAP_PARAM_IS_HIDDEN) != 0; }
bool ParamInfo::readonly() const { return (flags & CLAP_PARAM_IS_READONLY) != 0; }
bool ParamInfo::stepped() const { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
bool ParamInfo::automatable() const
{
    constexpr clap_param_info_flags anyAutomate =
        CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID |
        CLAP_PARAM_IS_AUTOMATABLE_PER_KEY | CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL |
        CLAP_PARAM_IS_AUTOMATABLE_PER_PORT;
    return (flags & anyAutomate) != 0;
}

std::optional<ParamsExt> ParamsExt::create(Plugin &plugin)
{
    const auto *ext =
        static_cast<const clap_plugin_params_t *>(plugin.getExtension(CLAP_EXT_PARAMS));
    if (!ext)
    {
        return std::nullopt;
    }
    return ParamsExt(plugin.clapPlugin(), ext);
}

ParamInfoMap ParamsExt::info() const
{
    ParamInfoMap result;
    uint32_t numParams = params_->count(plugin_);
    std::optional<clap_id> bypassParamId;

    for (uint32_t i = 0; i < numParams; ++i)
    {
        clap_param_info_t info = {};
        if (!params_->get_info(plugin_, i, &info))
        {
            fail("Plugin returned an error when querying parameter " + std::to_string(i) + ".");
        }

        std::string name = fixedToString(info.name, CLAP_NAME_SIZE);
        // NOTE: the Rust validator intended to validate the parameter's module string for stray
        // slashes, but a copy-paste bug made it read the *name* field instead, so in practice it
        // validated nothing here. We omit the check rather than introduce a stricter one, since a
        // leading slash in a module path is used by real, working plugins (e.g. Surge XT) and
        // enforcing it would diverge from the reference validator's behavior.

        if (info.min_value > info.max_value)
        {
            fail("Parameter '" + name + "' has a minimum value higher than its maximum value.");
        }
        if (info.default_value < info.min_value || info.default_value > info.max_value)
        {
            fail("Parameter '" + name + "' has a default value outside of its range.");
        }
        if ((info.flags & CLAP_PARAM_IS_STEPPED) != 0)
        {
            if (info.min_value != std::trunc(info.min_value) ||
                info.max_value != std::trunc(info.max_value))
            {
                fail("Parameter '" + name + "' is stepped but its range is not integral.");
            }
        }
        if ((info.flags & CLAP_PARAM_IS_BYPASS) != 0)
        {
            if (bypassParamId.has_value())
            {
                fail("The plugin has multiple bypass parameters.");
            }
            bypassParamId = info.id;
            if ((info.flags & CLAP_PARAM_IS_STEPPED) == 0)
            {
                fail("Parameter '" + name + "' is a bypass parameter but is not stepped.");
            }
        }

        // CLAP_PARAM_IS_ENUM requires CLAP_PARAM_IS_STEPPED (params.h).
        if ((info.flags & CLAP_PARAM_IS_ENUM) != 0 && (info.flags & CLAP_PARAM_IS_STEPPED) == 0)
        {
            fail("Parameter '" + name +
                 "' is an enum parameter but is not stepped; "
                 "CLAP_PARAM_IS_ENUM requires CLAP_PARAM_IS_STEPPED.");
        }

        constexpr clap_param_info_flags perAutomate =
            CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID | CLAP_PARAM_IS_AUTOMATABLE_PER_KEY |
            CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL | CLAP_PARAM_IS_AUTOMATABLE_PER_PORT;
        constexpr clap_param_info_flags perModulate =
            CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
            CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
        if ((info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0 && (info.flags & perAutomate) != 0)
        {
            fail("Parameter '" + name +
                 "' is automatable per note/key/channel/port but not automatable.");
        }
        if ((info.flags & CLAP_PARAM_IS_MODULATABLE) == 0 && (info.flags & perModulate) != 0)
        {
            fail("Parameter '" + name +
                 "' is modulatable per note/key/channel/port but not modulatable.");
        }
        if ((info.flags & CLAP_PARAM_IS_READONLY) != 0 &&
            ((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0 ||
             (info.flags & CLAP_PARAM_IS_MODULATABLE) != 0))
        {
            fail("Parameter '" + name + "' is readonly but also automatable or modulatable.");
        }

        ParamInfo processed;
        processed.id = info.id;
        processed.name = name;
        processed.module = fixedToString(info.module, CLAP_PATH_SIZE);
        processed.cookie = info.cookie;
        processed.minValue = info.min_value;
        processed.maxValue = info.max_value;
        processed.defaultValue = info.default_value;
        processed.flags = info.flags;

        if (!result.emplace(info.id, processed).second)
        {
            fail("The plugin contains multiple parameters with stable ID " +
                 std::to_string(info.id) + ".");
        }
    }

    return result;
}

double ParamsExt::getValue(clap_id paramId) const
{
    double value = 0.0;
    if (!params_->get_value(plugin_, paramId, &value))
    {
        fail("'clap_plugin_params::get_value()' returned false for parameter ID " +
             std::to_string(paramId) + ".");
    }
    return value;
}

std::optional<std::string> ParamsExt::valueToText(clap_id paramId, double value) const
{
    char buffer[CLAP_NAME_SIZE] = {};
    if (!params_->value_to_text(plugin_, paramId, value, buffer, sizeof(buffer)))
    {
        return std::nullopt;
    }
    return fixedToString(buffer, sizeof(buffer));
}

std::optional<double> ParamsExt::textToValue(clap_id paramId, const std::string &text) const
{
    double value = 0.0;
    if (!params_->text_to_value(plugin_, paramId, text.c_str(), &value))
    {
        return std::nullopt;
    }
    return value;
}

void ParamsExt::flush(EventList &inputEvents, EventList &outputEvents) const
{
    params_->flush(plugin_, inputEvents.inputVTable(), outputEvents.outputVTable());
}

} // namespace clap_validator
