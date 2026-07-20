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
#include "plugin_tests.h"
#include "../plugin/library.h"
#include "../plugin/host.h"
#include "../plugin/instance.h"
#include "../plugin/process.h"
#include "../plugin/ext.h"
#include "../util.h"
#include "rng.h"
#include "processing_test.h"
#include <set>
#include <cmath>
#include <cstring>
#include <map>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace clap_validator
{

namespace
{

// Save the plugin's state into a byte vector. maxBytesPerWrite caps how many bytes each write()
// call accepts, used to exercise buffered writes. Throws if the plugin fails to save.
std::vector<uint8_t> saveState(const clap_plugin_state_t *state, const clap_plugin_t *plugin,
                               size_t maxBytesPerWrite = std::numeric_limits<size_t>::max())
{
    struct Writer
    {
        std::vector<uint8_t> data;
        size_t maxBytesPerWrite;
    } writer{{}, maxBytesPerWrite};

    clap_ostream_t stream = {};
    stream.ctx = &writer;
    stream.write = [](const clap_ostream_t *s, const void *buffer, uint64_t size) -> int64_t
    {
        auto *self = static_cast<Writer *>(s->ctx);
        uint64_t n = std::min<uint64_t>(size, self->maxBytesPerWrite);
        const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
        self->data.insert(self->data.end(), bytes, bytes + n);
        return static_cast<int64_t>(n);
    };

    if (!state->save(plugin, &stream))
    {
        throw std::runtime_error("The plugin failed to save its state.");
    }
    return writer.data;
}

// Load state from a byte vector. maxBytesPerRead caps how many bytes each read() call returns,
// used to exercise buffered reads. Returns the plugin's load() result.
bool loadState(const clap_plugin_state_t *state, const clap_plugin_t *plugin,
               const std::vector<uint8_t> &data,
               size_t maxBytesPerRead = std::numeric_limits<size_t>::max())
{
    struct Reader
    {
        const std::vector<uint8_t> *data;
        size_t pos;
        size_t maxBytesPerRead;
    } reader{&data, 0, maxBytesPerRead};

    clap_istream_t stream = {};
    stream.ctx = &reader;
    stream.read = [](const clap_istream_t *s, void *buffer, uint64_t size) -> int64_t
    {
        auto *self = static_cast<Reader *>(s->ctx);
        size_t available = self->data->size() - self->pos;
        uint64_t n = std::min<uint64_t>(
            {size, static_cast<uint64_t>(available), static_cast<uint64_t>(self->maxBytesPerRead)});
        if (n > 0)
        {
            std::memcpy(buffer, self->data->data() + self->pos, n);
            self->pos += n;
        }
        return static_cast<int64_t>(n);
    };

    return state->load(plugin, &stream);
}

// Query every parameter's current value.
std::map<clap_id, double> getAllParamValues(const ParamsExt &params, const ParamInfoMap &infos)
{
    std::map<clap_id, double> values;
    for (const auto &entry : infos)
    {
        values[entry.first] = params.getValue(entry.first);
    }
    return values;
}

// Set by PluginTests::setFullOutput; when true, detail lists are shown in full.
bool g_fullOutput = false;

// Format a double with enough significant digits to reveal small differences (std::to_string only
// prints six decimals, which can make two genuinely different values look identical).
std::string formatDouble(double value, int precision = 12)
{
    std::ostringstream os;
    os << std::setprecision(precision) << value;
    return os.str();
}

// Format a list of items as a multi-line block, one per line, truncated to the first few (unless
// full output is requested) followed by a count of the rest. Meant to follow a trailing ':'.
std::string formatTruncatedList(const std::vector<std::string> &items)
{
    const size_t kMaxShown = g_fullOutput ? items.size() : std::min<size_t>(items.size(), 7);
    std::string result;
    for (size_t i = 0; i < kMaxShown; ++i)
    {
        result += "\n  - " + items[i];
    }
    if (items.size() > kMaxShown)
    {
        result += "\n  ... and " + std::to_string(items.size() - kMaxShown) +
                  " more. Run with --full-output to see all";
    }
    return result;
}

// Build a human-readable, truncated list of the parameters whose values differ. The result is a
// multi-line block (each entry on its own line) meant to follow a trailing ':'.
std::string formatMismatchingValues(const std::map<clap_id, double> &actual,
                                    const std::map<clap_id, double> &expected,
                                    const ParamInfoMap &infos)
{
    std::vector<std::string> items;
    for (const auto &[id, actualValue] : actual)
    {
        auto it = expected.find(id);
        double expectedValue = it != expected.end() ? it->second : 0.0;
        if (actualValue == expectedValue)
        {
            continue;
        }
        std::string name = infos.count(id) ? infos.at(id).name : std::string();
        items.push_back("parameter " + std::to_string(id) + " ('" + name + "'): expected " +
                        formatDouble(expectedValue) + ", actual " + formatDouble(actualValue) +
                        " (diff " + formatDouble(actualValue - expectedValue, 6) + ")");
    }
    return formatTruncatedList(items);
}

} // namespace

void PluginTests::setFullOutput(bool full) { g_fullOutput = full; }

std::vector<TestCaseInfo> PluginTests::getAllTests()
{
    return {
        // Descriptor tests
        {"descriptor-consistency",
         "The plugin descriptor returned from the plugin factory and the plugin descriptor stored "
         "on the 'clap_plugin' object should be equivalent."},
        {"features-categories",
         "The plugin needs to have at least one of the main CLAP category features."},
        {"features-duplicates", "The plugin's features array should not contain any duplicates."},

        // Processing tests
        {"process-audio-out-of-place-basic",
         "Processes random audio through the plugin with its default parameter values and tests "
         "whether the output does not contain any non-finite or subnormal values. Uses "
         "out-of-place "
         "audio processing."},
        {"process-note-out-of-place-basic",
         "Sends audio and random note and MIDI events to the plugin with its default parameter "
         "values and tests the output for consistency. Uses out-of-place audio processing."},
        {"process-note-inconsistent",
         "Sends intentionally inconsistent and mismatching note and MIDI events to the plugin with "
         "its default parameter values and tests the output for consistency."},

        // Parameter tests
        {"param-conversions",
         "Asserts that value to string and string to value conversions are supported for either "
         "all or none of the plugin's parameters, and that conversions between values and strings "
         "roundtrip consistently."},
        {"param-fuzz-basic",
         "Generates random parameter values, sets those on the plugin, and has the plugin process "
         "buffers of random audio and note events. The plugin passes the test if it doesn't "
         "produce any infinite or NaN values, and doesn't crash."},
        {"param-set-wrong-namespace",
         "Sends events to the plugin with the 'CLAP_EVENT_PARAM_VALUE' event type but with a "
         "mismatching namespace ID. Asserts that the plugin's parameter values don't change."},

        // State tests
        {"state-invalid",
         "The plugin should return false when 'clap_plugin_state::load()' is called with an empty "
         "state."},
        {"state-reproducibility-basic",
         "Randomizes a plugin's parameters, saves its state, recreates the plugin instance, "
         "reloads the state, and then checks whether the parameter values are the same and whether "
         "saving the state once more results in the same state file as before."},
        {"state-reproducibility-null-cookies",
         "The exact same test as state-reproducibility-basic, but with all cookies in the "
         "parameter events set to null pointers."},
        {"state-reproducibility-flush",
         "Randomizes a plugin's parameters, saves its state, recreates the plugin instance, sets "
         "the same parameters as before, saves the state again, and then asserts that the two "
         "states are identical. Uses flush function for the second state."},
        {"state-buffered-streams",
         "Performs the same state and parameter reproducibility check, but the plugin is only "
         "allowed to read a small prime number of bytes at a time when reloading and resaving the "
         "state."},

        // Other extension tests
        {"context-menu",
         "If the plugin implements the 'context-menu' extension, populates its global and "
         "per-parameter context menus and checks that every menu item is well-formed: non-null "
         "labels and titles, balanced submenus, and known item kinds."},
        {"latency",
         "If the plugin implements the 'latency' extension, checks that its reported latency is "
         "readable while the plugin is active and stable across reads."},
        {"tail",
         "If the plugin implements the 'tail' extension, checks that its reported tail length is "
         "readable and stable."},
        {"voice-info",
         "If the plugin implements the 'voice-info' extension, checks that it reports "
         "1 <= voice_count <= voice_capacity while the plugin is active."},
        {"note-name",
         "If the plugin implements the 'note-name' extension, checks that every declared note name "
         "can be queried and has valid key, channel, and port ranges."},
        {"render",
         "If the plugin implements the 'render' extension, checks the render mode setters: the "
         "realtime mode is accepted and a plugin with a hard "
         "realtime requirement rejects the offline mode."},
        {"param-defaults",
         "Checks that a freshly created plugin reports each parameter's declared default value "
         "before any state is loaded."},
        {"param-info-stable", "Checks that the plugin's parameter information (ids, cookies, "
                              "ranges, flags) is identical "
                              "across repeated queries."},
        {"audio-ports-config",
         "If the plugin implements the 'audio-ports-config' extension, enumerates its port "
         "configurations, selects each one, and checks that the plugin's audio ports then match "
         "the "
         "selected configuration."}};
}

TestResult PluginTests::runTest(const std::string &testName, PluginLibrary &library,
                                const std::string &pluginId)
{
    // Descriptor tests
    if (testName == "descriptor-consistency")
    {
        return testDescriptorConsistency(library, pluginId);
    }
    else if (testName == "features-categories")
    {
        return testFeaturesCategories(library, pluginId);
    }
    else if (testName == "features-duplicates")
    {
        return testFeaturesDuplicates(library, pluginId);
    }
    // Processing tests
    else if (testName == "process-audio-out-of-place-basic")
    {
        return testProcessAudioOutOfPlaceBasic(library, pluginId);
    }
    else if (testName == "process-note-out-of-place-basic")
    {
        return testProcessNoteOutOfPlaceBasic(library, pluginId);
    }
    else if (testName == "process-note-inconsistent")
    {
        return testProcessNoteInconsistent(library, pluginId);
    }
    // Parameter tests
    else if (testName == "param-conversions")
    {
        return testParamConversions(library, pluginId);
    }
    else if (testName == "param-fuzz-basic")
    {
        return testParamFuzzBasic(library, pluginId);
    }
    else if (testName == "param-set-wrong-namespace")
    {
        return testParamSetWrongNamespace(library, pluginId);
    }
    // State tests
    else if (testName == "state-invalid")
    {
        return testStateInvalid(library, pluginId);
    }
    else if (testName == "state-reproducibility-basic")
    {
        return testStateReproducibilityBasic(library, pluginId);
    }
    else if (testName == "state-reproducibility-null-cookies")
    {
        return testStateReproducibilityNullCookies(library, pluginId);
    }
    else if (testName == "state-reproducibility-flush")
    {
        return testStateReproducibilityFlush(library, pluginId);
    }
    else if (testName == "state-buffered-streams")
    {
        return testStateBufferedStreams(library, pluginId);
    }
    // Other extension tests
    else if (testName == "context-menu")
    {
        return testContextMenu(library, pluginId);
    }
    else if (testName == "latency")
    {
        return testLatency(library, pluginId);
    }
    else if (testName == "tail")
    {
        return testTail(library, pluginId);
    }
    else if (testName == "voice-info")
    {
        return testVoiceInfo(library, pluginId);
    }
    else if (testName == "note-name")
    {
        return testNoteName(library, pluginId);
    }
    else if (testName == "render")
    {
        return testRender(library, pluginId);
    }
    else if (testName == "param-defaults")
    {
        return testParamDefaults(library, pluginId);
    }
    else if (testName == "param-info-stable")
    {
        return testParamInfoStable(library, pluginId);
    }
    else if (testName == "audio-ports-config")
    {
        return testAudioPortsConfig(library, pluginId);
    }

    return TestResult::failed(testName, "Unknown test", "Test '" + testName + "' not found");
}

TestResult PluginTests::testDescriptorConsistency(PluginLibrary &library,
                                                  const std::string &pluginId)
{
    const std::string testName = "descriptor-consistency";
    const std::string description =
        "The plugin descriptor returned from the plugin factory and the plugin descriptor stored "
        "on the 'clap_plugin' object should be equivalent.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const clap_plugin_descriptor_t *instanceDesc = plugin->descriptor();
        if (!instanceDesc)
        {
            return TestResult::failed(testName, description, "Plugin instance has no descriptor");
        }

        // Find the matching descriptor straight from the factory so we can compare every field.
        const clap_plugin_factory_t *factory = library.getPluginFactory();
        if (!factory)
        {
            return TestResult::failed(testName, description, "Library has no plugin factory");
        }
        const clap_plugin_descriptor_t *factoryDesc = nullptr;
        uint32_t count = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < count; ++i)
        {
            const clap_plugin_descriptor_t *d = factory->get_plugin_descriptor(factory, i);
            if (d && d->id && pluginId == d->id)
            {
                factoryDesc = d;
                break;
            }
        }
        if (!factoryDesc)
        {
            return TestResult::failed(testName, description, "Plugin ID not found in factory");
        }

        // Compare a single (possibly null) descriptor string field.
        auto compareField = [&](const char *fieldName, const char *factoryValue,
                                const char *instanceValue) -> std::optional<std::string>
        {
            bool factoryNull = factoryValue == nullptr;
            bool instanceNull = instanceValue == nullptr;
            if (factoryNull != instanceNull ||
                (!factoryNull && std::strcmp(factoryValue, instanceValue) != 0))
            {
                return std::string("The '") + fieldName +
                       "' field differs between the factory descriptor ('" +
                       (factoryNull ? "<null>" : factoryValue) +
                       "') and the instance descriptor ('" +
                       (instanceNull ? "<null>" : instanceValue) + "').";
            }
            return std::nullopt;
        };

        std::optional<std::string> mismatch;
        if (!(mismatch = compareField("id", factoryDesc->id, instanceDesc->id)) &&
            !(mismatch = compareField("name", factoryDesc->name, instanceDesc->name)) &&
            !(mismatch = compareField("vendor", factoryDesc->vendor, instanceDesc->vendor)) &&
            !(mismatch = compareField("url", factoryDesc->url, instanceDesc->url)) &&
            !(mismatch =
                  compareField("manual_url", factoryDesc->manual_url, instanceDesc->manual_url)) &&
            !(mismatch = compareField("support_url", factoryDesc->support_url,
                                      instanceDesc->support_url)) &&
            !(mismatch = compareField("version", factoryDesc->version, instanceDesc->version)) &&
            !(mismatch =
                  compareField("description", factoryDesc->description, instanceDesc->description)))
        {
            // All scalar fields matched; now compare the features array in order.
            std::vector<std::string> factoryFeatures = cstrArrayToVector(factoryDesc->features);
            std::vector<std::string> instanceFeatures = cstrArrayToVector(instanceDesc->features);
            if (factoryFeatures != instanceFeatures)
            {
                mismatch = std::string("The 'features' arrays differ between the factory and "
                                       "instance descriptors.");
            }
        }

        if (mismatch)
        {
            return TestResult::failed(testName, description, *mismatch);
        }

        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testFeaturesCategories(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "features-categories";
    const std::string description = "Plugin must have at least one main category feature.";

    try
    {
        auto metadata = library.metadata();

        const PluginMetadata *pluginMeta = nullptr;
        for (const auto &pm : metadata.plugins)
        {
            if (pm.id == pluginId)
            {
                pluginMeta = &pm;
                break;
            }
        }

        if (!pluginMeta)
        {
            return TestResult::failed(testName, description, "Plugin ID not found");
        }

        // Main CLAP category features
        const std::set<std::string> mainCategories = {
            CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
            CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_NOTE_DETECTOR,
            CLAP_PLUGIN_FEATURE_ANALYZER};

        bool hasMainCategory = false;
        for (const auto &feature : pluginMeta->features)
        {
            if (mainCategories.count(feature))
            {
                hasMainCategory = true;
                break;
            }
        }

        if (!hasMainCategory)
        {
            return TestResult::failed(testName, description,
                                      "Plugin does not have any main category feature (instrument, "
                                      "audio-effect, note-effect, note-detector, analyzer)");
        }

        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testFeaturesDuplicates(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "features-duplicates";
    const std::string description = "Plugin features should not contain duplicates.";

    try
    {
        auto metadata = library.metadata();

        const PluginMetadata *pluginMeta = nullptr;
        for (const auto &pm : metadata.plugins)
        {
            if (pm.id == pluginId)
            {
                pluginMeta = &pm;
                break;
            }
        }

        if (!pluginMeta)
        {
            return TestResult::failed(testName, description, "Plugin ID not found");
        }

        std::set<std::string> seenFeatures;
        for (const auto &feature : pluginMeta->features)
        {
            if (seenFeatures.count(feature))
            {
                return TestResult::failed(testName, description,
                                          "Duplicate feature found: '" + feature + "'");
            }
            seenFeatures.insert(feature);
        }

        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testProcessAudioOutOfPlaceBasic(PluginLibrary &library,
                                                        const std::string &pluginId)
{
    const std::string testName = "process-audio-out-of-place-basic";
    const std::string description =
        "Processes random audio through the plugin with its default parameter values and tests "
        "whether the output does not contain any non-finite or subnormal values. Uses out-of-place "
        "audio processing.";

    try
    {
        Prng prng = newPrng();

        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        auto audioConfig = AudioPortConfig::query(*plugin);
        if (!audioConfig)
        {
            return TestResult::skipped(
                testName, description,
                "The plugin does not implement the 'audio-ports' extension.");
        }
        host->handleCallbacksOnce();

        AudioBuffers buffers(*audioConfig, BUFFER_SIZE);
        ProcessingTest processingTest(*plugin, host, buffers);
        processingTest.run(5, ProcessConfig{}, [&](ProcessData &) { buffers.randomize(prng); });

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testParamConversions(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "param-conversions";
    const std::string description =
        "Asserts that value to string and string to value conversions are supported for either all "
        "or none of the plugin's parameters, and that conversions between values and strings "
        "roundtrip consistently.";

    try
    {
        Prng prng = newPrng();

        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        auto params = ParamsExt::create(*plugin);
        if (!params)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        host->handleCallbacksOnce();

        ParamInfoMap paramInfos = params->info();

        // A plugin should support each conversion for either all of its parameters or for none of
        // them. We test six values per parameter: the minimum and maximum (which may have special
        // meaning) plus four random values in range. Stepped parameters only accept integer values,
        // so their random values are rounded to a valid step - feeding a stepped parameter a
        // fractional value is not a meaningful test.
        constexpr int kValuesPerParam = 6;
        size_t expectedConversions = paramInfos.size() * kValuesPerParam;
        size_t numSupportedValueToText = 0;
        size_t numSupportedTextToValue = 0;
        std::vector<std::string> failedValueToText;
        std::vector<std::string> failedTextToValue;

        for (const auto &[paramId, info] : paramInfos)
        {
            auto pick = [&]()
            {
                double value = prng.nextDouble(info.minValue, info.maxValue);
                return info.stepped() ? std::round(value) : value;
            };
            double values[kValuesPerParam] = {info.minValue, info.maxValue, pick(),
                                              pick(),        pick(),        pick()};

            for (double startingValue : values)
            {
                // Build up a human-readable description of the roundtrip as we go, so failure
                // messages can show exactly what was converted into what.
                std::string chain = "set " + std::to_string(startingValue);

                // If the plugin rounds string representations then a raw value may not roundtrip,
                // so we start from the plugin's own string representation.
                auto startingText = params->valueToText(paramId, startingValue);
                if (!startingText)
                {
                    // value_to_text unsupported for this parameter; skip the rest of it.
                    failedValueToText.push_back("parameter '" + info.name + "': " + chain +
                                                " -> value_to_text unsupported");
                    break;
                }
                numSupportedValueToText++;
                chain += " -> '" + *startingText + "'";

                auto reconvertedValue = params->textToValue(paramId, *startingText);
                if (!reconvertedValue)
                {
                    // text_to_value unsupported; keep testing value_to_text on the next value.
                    failedTextToValue.push_back("parameter '" + info.name + "': " + chain +
                                                " -> text_to_value unsupported");
                    continue;
                }
                numSupportedTextToValue++;
                chain += " -> " + std::to_string(*reconvertedValue);

                auto reconvertedText = params->valueToText(paramId, *reconvertedValue);
                if (!reconvertedText)
                {
                    throw std::runtime_error("Repeated value-to-text conversion failed for "
                                             "parameter '" +
                                             info.name + "' (" + chain + ").");
                }
                chain += " -> '" + *reconvertedText + "'";

                // Both strings come from the plugin, so they should be identical.
                if (*startingText != *reconvertedText)
                {
                    throw std::runtime_error("Value/string conversions for parameter '" +
                                             info.name + "' are not consistent: " + chain + " ('" +
                                             *reconvertedText + "' should equal '" + *startingText +
                                             "').");
                }

                auto finalValue = params->textToValue(paramId, *reconvertedText);
                if (!finalValue)
                {
                    throw std::runtime_error("Repeated text-to-value conversion failed for "
                                             "parameter '" +
                                             info.name + "' (" + chain + ").");
                }
                chain += " -> " + std::to_string(*finalValue);

                if (*finalValue != *reconvertedValue)
                {
                    throw std::runtime_error("Value/string conversions for parameter '" +
                                             info.name + "' do not roundtrip: " + chain + " (" +
                                             std::to_string(*finalValue) + " should equal " +
                                             std::to_string(*reconvertedValue) + ").");
                }
            }
        }

        // A stepped parameter's rounded values often collapse to the same step, so drop duplicate
        // entries to keep the reported list concise.
        auto dedupe = [](std::vector<std::string> &items)
        {
            std::set<std::string> seen;
            std::vector<std::string> unique;
            for (auto &item : items)
            {
                if (seen.insert(item).second)
                {
                    unique.push_back(item);
                }
            }
            items = std::move(unique);
        };
        dedupe(failedValueToText);
        dedupe(failedTextToValue);

        if (numSupportedValueToText != 0 && numSupportedValueToText != expectedConversions)
        {
            throw std::runtime_error(
                "'clap_plugin_params::value_to_text()' succeeded for " +
                std::to_string(numSupportedValueToText) + " out of " +
                std::to_string(expectedConversions) +
                " calls; it should be supported for either all parameters or none. The conversions "
                "that were not supported:" +
                formatTruncatedList(failedValueToText));
        }
        if (numSupportedTextToValue != 0 && numSupportedTextToValue != expectedConversions)
        {
            throw std::runtime_error(
                "'clap_plugin_params::text_to_value()' succeeded for " +
                std::to_string(numSupportedTextToValue) + " out of " +
                std::to_string(expectedConversions) +
                " calls; it should be supported for either all parameters or none. The conversions "
                "that were not supported:" +
                formatTruncatedList(failedTextToValue));
        }

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }

        if (numSupportedValueToText == 0 || numSupportedTextToValue == 0)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin's parameters need to support both value-to-text "
                                       "and text-to-value conversions for this test.");
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testProcessNoteOutOfPlaceBasic(PluginLibrary &library,
                                                       const std::string &pluginId)
{
    const std::string testName = "process-note-out-of-place-basic";
    const std::string description =
        "Sends audio and random note and MIDI events to the plugin with its default parameter "
        "values and tests the output for consistency. Uses out-of-place audio processing.";

    try
    {
        Prng prng = newPrng();

        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        // Note/MIDI-only plugins are fine, so missing audio ports is not an error here.
        auto audioConfig = AudioPortConfig::query(*plugin).value_or(AudioPortConfig{});
        auto noteConfig = NotePortConfig::query(*plugin);
        if (!noteConfig)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'note-ports' extension.");
        }
        if (noteConfig->inputs.empty())
        {
            return TestResult::skipped(
                testName, description,
                "The plugin implements 'note-ports' but has no input note ports.");
        }
        host->handleCallbacksOnce();

        NoteGenerator noteGen(*noteConfig);
        AudioBuffers buffers(audioConfig, BUFFER_SIZE);
        ProcessingTest processingTest(*plugin, host, buffers);
        processingTest.run(5, ProcessConfig{},
                           [&](ProcessData &processData)
                           {
                               noteGen.fillEventQueue(prng, processData.inputEvents(), BUFFER_SIZE);
                               buffers.randomize(prng);
                           });

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testProcessNoteInconsistent(PluginLibrary &library,
                                                    const std::string &pluginId)
{
    const std::string testName = "process-note-inconsistent";
    const std::string description =
        "Sends intentionally inconsistent and mismatching note and MIDI events to the plugin with "
        "its default parameter values and tests the output for consistency.";

    try
    {
        Prng prng = newPrng();

        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        auto audioConfig = AudioPortConfig::query(*plugin).value_or(AudioPortConfig{});
        auto noteConfig = NotePortConfig::query(*plugin);
        if (!noteConfig)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'note-ports' extension.");
        }
        if (noteConfig->inputs.empty())
        {
            return TestResult::skipped(
                testName, description,
                "The plugin implements 'note-ports' but has no input note ports.");
        }
        host->handleCallbacksOnce();

        NoteGenerator noteGen(*noteConfig);
        noteGen.withInconsistentEvents();
        AudioBuffers buffers(audioConfig, BUFFER_SIZE);
        ProcessingTest processingTest(*plugin, host, buffers);
        processingTest.run(5, ProcessConfig{},
                           [&](ProcessData &processData)
                           {
                               noteGen.fillEventQueue(prng, processData.inputEvents(), BUFFER_SIZE);
                               buffers.randomize(prng);
                           });

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testParamFuzzBasic(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "param-fuzz-basic";
    const std::string description =
        "Generates random parameter values, sets those on the plugin, and has the plugin process "
        "buffers of random audio and note events. The plugin passes the test if it doesn't produce "
        "any infinite or NaN values, and doesn't crash.";

    try
    {
        Prng prng = newPrng();

        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        // Audio and note ports are both optional; only the params extension is required.
        auto audioConfig = AudioPortConfig::query(*plugin).value_or(AudioPortConfig{});
        auto noteConfig = NotePortConfig::query(*plugin);
        auto params = ParamsExt::create(*plugin);
        if (!params)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        host->handleCallbacksOnce();

        ParamInfoMap paramInfos = params->info();
        ParamFuzzer paramFuzzer(paramInfos);

        // Only generate notes if the plugin actually has input note ports (JUCE exposes the
        // extension with no ports).
        std::optional<NoteGenerator> noteGen;
        if (noteConfig && !noteConfig->inputs.empty())
        {
            noteGen.emplace(*noteConfig);
        }

        AudioBuffers buffers(audioConfig, BUFFER_SIZE);
        ProcessingTest processingTest(*plugin, host, buffers);

        for (size_t permutation = 1; permutation <= FUZZ_NUM_PERMUTATIONS; ++permutation)
        {
            // The parameter values for this permutation are injected once at the start; the plugin
            // then processes several buffers so its state can settle before the next permutation.
            EventList paramEventQueue;
            paramFuzzer.randomizeParamsAt(prng, 0, paramEventQueue);
            std::vector<Event> paramEvents = paramEventQueue.events();

            bool haveSetParameters = false;
            try
            {
                processingTest.run(static_cast<int>(FUZZ_RUNS_PER_PERMUTATION), ProcessConfig{},
                                   [&](ProcessData &processData)
                                   {
                                       if (!haveSetParameters)
                                       {
                                           for (const auto &event : paramEvents)
                                           {
                                               processData.inputEvents().push(event);
                                           }
                                           haveSetParameters = true;
                                       }
                                       if (noteGen)
                                       {
                                           noteGen->fillEventQueue(prng, processData.inputEvents(),
                                                                   BUFFER_SIZE);
                                       }
                                       buffers.randomize(prng);
                                   });
            }
            catch (const std::exception &e)
            {
                return TestResult::failed(
                    testName, description,
                    "Invalid output detected in parameter value permutation " +
                        std::to_string(permutation) + " of " +
                        std::to_string(FUZZ_NUM_PERMUTATIONS) + ": " + e.what());
            }
        }

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testParamSetWrongNamespace(PluginLibrary &library,
                                                   const std::string &pluginId)
{
    const std::string testName = "param-set-wrong-namespace";
    const std::string description =
        "Sends events to the plugin with the 'CLAP_EVENT_PARAM_VALUE' event type but with a "
        "mismatching namespace ID. Asserts that the plugin's parameter values don't change.";

    try
    {
        Prng prng = newPrng();

        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        auto audioConfig = AudioPortConfig::query(*plugin).value_or(AudioPortConfig{});
        auto params = ParamsExt::create(*plugin);
        if (!params)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        host->handleCallbacksOnce();

        ParamInfoMap paramInfos = params->info();
        std::map<clap_id, double> initialValues;
        for (const auto &entry : paramInfos)
        {
            initialValues[entry.first] = params->getValue(entry.first);
        }

        // Build random parameter events but with the wrong namespace id. The plugin should ignore
        // them, so its parameter values should stay unchanged.
        constexpr uint16_t kIncorrectNamespaceId = 0xb33f;
        EventList paramEventQueue;
        ParamFuzzer(paramInfos).randomizeParamsAt(prng, 0, paramEventQueue);
        std::vector<Event> paramEvents = paramEventQueue.events();
        for (auto &event : paramEvents)
        {
            event.header()->space_id = kIncorrectNamespaceId;
        }

        AudioBuffers buffers(audioConfig, BUFFER_SIZE);
        ProcessingTest processingTest(*plugin, host, buffers);
        processingTest.runOnce(ProcessConfig{},
                               [&](ProcessData &processData)
                               {
                                   for (const auto &event : paramEvents)
                                   {
                                       processData.inputEvents().push(event);
                                   }
                               });

        std::map<clap_id, double> actualValues;
        for (const auto &entry : paramInfos)
        {
            actualValues[entry.first] = params->getValue(entry.first);
        }

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }

        if (actualValues == initialValues)
        {
            return TestResult::success(testName, description);
        }
        return TestResult::failed(
            testName, description,
            "Sending CLAP_EVENT_PARAM_VALUE events with namespace ID 0xb33f to the plugin caused "
            "its parameter values to change. This should not happen; the plugin may not be "
            "checking "
            "the event's namespace ID.");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testStateInvalid(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "state-invalid";
    const std::string description = "Tests that plugin rejects invalid/empty state.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const clap_plugin_state_t *stateExt =
            static_cast<const clap_plugin_state_t *>(plugin->getExtension(CLAP_EXT_STATE));

        if (!stateExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support state extension");
        }

        // Create an empty stream for loading
        struct EmptyStream
        {
            static int64_t read(const clap_istream_t *, void *, uint64_t) { return 0; }
        };

        clap_istream_t emptyStream = {};
        emptyStream.ctx = nullptr;
        emptyStream.read = EmptyStream::read;

        // Plugin should return false for empty state
        bool loadResult = stateExt->load(plugin->clapPlugin(), &emptyStream);

        if (loadResult)
        {
            return TestResult::failed(
                testName, description,
                "Plugin returned true when loading empty state (should return false)");
        }

        return TestResult::success(testName, description, "Plugin correctly rejected empty state");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testStateReproducibilityBasic(PluginLibrary &library,
                                                      const std::string &pluginId)
{
    return testStateReproducibilityImpl(library, pluginId, false);
}

TestResult PluginTests::testStateReproducibilityNullCookies(PluginLibrary &library,
                                                            const std::string &pluginId)
{
    return testStateReproducibilityImpl(library, pluginId, true);
}

TestResult PluginTests::testStateReproducibilityImpl(PluginLibrary &library,
                                                     const std::string &pluginId,
                                                     bool zeroOutCookies)
{
    const std::string testName =
        zeroOutCookies ? "state-reproducibility-null-cookies" : "state-reproducibility-basic";
    const std::string description =
        zeroOutCookies
            ? "The exact same test as state-reproducibility-basic, but with all cookies in the "
              "parameter events set to null pointers."
            : "Randomizes a plugin's parameters, saves its state, recreates the plugin instance, "
              "reloads the state, and then checks whether the parameter values are the same and "
              "whether saving the state once more results in the same state file as before.";

    try
    {
        Prng prng = newPrng();
        auto host = std::make_shared<Host>();

        std::vector<uint8_t> expectedState;
        std::map<clap_id, double> expectedValues;
        ParamInfoMap paramInfos;

        // First instance: randomize the parameters through processing, then capture the state.
        {
            auto plugin = library.createPlugin(pluginId, host);
            if (!plugin->init())
            {
                return TestResult::failed(testName, description, "Failed to initialize plugin");
            }

            auto audioConfig = AudioPortConfig::query(*plugin).value_or(AudioPortConfig{});
            auto params = ParamsExt::create(*plugin);
            if (!params)
            {
                return TestResult::skipped(testName, description,
                                           "The plugin does not implement the 'params' extension.");
            }
            const clap_plugin_state_t *stateExt =
                static_cast<const clap_plugin_state_t *>(plugin->getExtension(CLAP_EXT_STATE));
            if (!stateExt)
            {
                return TestResult::skipped(testName, description,
                                           "The plugin does not implement the 'state' extension.");
            }
            host->handleCallbacksOnce();

            paramInfos = params->info();
            EventList paramEventQueue;
            ParamFuzzer(paramInfos).randomizeParamsAt(prng, 0, paramEventQueue, zeroOutCookies);
            std::vector<Event> paramEvents = paramEventQueue.events();

            AudioBuffers buffers(audioConfig, BUFFER_SIZE);
            ProcessingTest(*plugin, host, buffers)
                .runOnce(ProcessConfig{},
                         [&](ProcessData &processData)
                         {
                             for (const auto &event : paramEvents)
                             {
                                 processData.inputEvents().push(event);
                             }
                         });

            expectedValues = getAllParamValues(*params, paramInfos);
            expectedState = saveState(stateExt, plugin->clapPlugin());
            host->handleCallbacksOnce();
        }

        // Second instance: load the state and confirm the values and re-saved state match.
        auto plugin2 = library.createPlugin(pluginId, host);
        if (!plugin2->init())
        {
            return TestResult::failed(testName, description,
                                      "Failed to initialize the second plugin instance");
        }
        auto params2 = ParamsExt::create(*plugin2);
        if (!params2)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        const clap_plugin_state_t *stateExt2 =
            static_cast<const clap_plugin_state_t *>(plugin2->getExtension(CLAP_EXT_STATE));
        if (!stateExt2)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'state' extension.");
        }
        host->handleCallbacksOnce();

        if (!loadState(stateExt2, plugin2->clapPlugin(), expectedState))
        {
            throw std::runtime_error("The plugin failed to load the state it had just saved.");
        }
        host->handleCallbacksOnce();

        std::map<clap_id, double> actualValues = getAllParamValues(*params2, paramInfos);
        if (actualValues != expectedValues)
        {
            throw std::runtime_error(
                "After reloading the state, the plugin's parameter values do not match the saved "
                "values:" +
                formatMismatchingValues(actualValues, expectedValues, params2->info()));
        }

        std::vector<uint8_t> actualState = saveState(stateExt2, plugin2->clapPlugin());
        host->handleCallbacksOnce();

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        if (actualState == expectedState)
        {
            return TestResult::success(testName, description);
        }
        return TestResult::failed(
            testName, description,
            "Re-saving the loaded state resulted in a different state file (" +
                std::to_string(expectedState.size()) + " bytes expected, " +
                std::to_string(actualState.size()) + " bytes actual).");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testStateReproducibilityFlush(PluginLibrary &library,
                                                      const std::string &pluginId)
{
    const std::string testName = "state-reproducibility-flush";
    const std::string description =
        "Randomizes a plugin's parameters, saves its state, recreates the plugin instance, sets "
        "the same parameters as before, saves the state again, and then asserts that the two "
        "states are identical. Uses the flush function for the first state.";

    try
    {
        Prng prng = newPrng();
        auto host = std::make_shared<Host>();

        std::vector<uint8_t> expectedState;
        std::vector<Event> paramEvents;
        std::map<clap_id, double> expectedValues;

        // First instance: set parameters via params.flush(), then capture the state.
        {
            auto plugin = library.createPlugin(pluginId, host);
            if (!plugin->init())
            {
                return TestResult::failed(testName, description, "Failed to initialize plugin");
            }

            auto params = ParamsExt::create(*plugin);
            if (!params)
            {
                return TestResult::skipped(testName, description,
                                           "The plugin does not implement the 'params' extension.");
            }
            const clap_plugin_state_t *stateExt =
                static_cast<const clap_plugin_state_t *>(plugin->getExtension(CLAP_EXT_STATE));
            if (!stateExt)
            {
                return TestResult::skipped(testName, description,
                                           "The plugin does not implement the 'state' extension.");
            }
            host->handleCallbacksOnce();

            ParamInfoMap paramInfos = params->info();
            std::map<clap_id, double> initialValues = getAllParamValues(*params, paramInfos);

            // The same events are used for flush here and for process() on the second instance.
            EventList paramEventQueue;
            ParamFuzzer(paramInfos).randomizeParamsAt(prng, 0, paramEventQueue);
            paramEvents = paramEventQueue.events();

            EventList inputEvents;
            for (const auto &event : paramEvents)
            {
                inputEvents.push(event);
            }
            EventList outputEvents;
            params->flush(inputEvents, outputEvents);
            host->handleCallbacksOnce();

            expectedValues = getAllParamValues(*params, paramInfos);
            expectedState = saveState(stateExt, plugin->clapPlugin());
            host->handleCallbacksOnce();

            // If nothing changed, the plugin has probably not implemented flush.
            if (expectedValues == initialValues && !paramInfos.empty())
            {
                throw std::runtime_error(
                    "'clap_plugin_params::flush()' was called with random parameter values, but "
                    "the plugin's reported parameter values did not change.");
            }
        }

        // Second instance: set the same values via process() and confirm the state matches.
        auto plugin2 = library.createPlugin(pluginId, host);
        if (!plugin2->init())
        {
            return TestResult::failed(testName, description,
                                      "Failed to initialize the second plugin instance");
        }
        auto audioConfig = AudioPortConfig::query(*plugin2).value_or(AudioPortConfig{});
        auto params2 = ParamsExt::create(*plugin2);
        if (!params2)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        const clap_plugin_state_t *stateExt2 =
            static_cast<const clap_plugin_state_t *>(plugin2->getExtension(CLAP_EXT_STATE));
        if (!stateExt2)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'state' extension.");
        }
        host->handleCallbacksOnce();

        // Reusing the events requires refreshing the cookies for this instance.
        ParamInfoMap paramInfos2 = params2->info();
        for (auto &event : paramEvents)
        {
            clap_id paramId = event.u.paramValue.param_id;
            auto it = paramInfos2.find(paramId);
            if (it == paramInfos2.end())
            {
                throw std::runtime_error("The second instance is missing parameter " +
                                         std::to_string(paramId) + ".");
            }
            event.u.paramValue.cookie = it->second.cookie;
        }

        AudioBuffers buffers(audioConfig, BUFFER_SIZE);
        ProcessingTest(*plugin2, host, buffers)
            .runOnce(ProcessConfig{},
                     [&](ProcessData &processData)
                     {
                         for (const auto &event : paramEvents)
                         {
                             processData.inputEvents().push(event);
                         }
                     });

        std::map<clap_id, double> actualValues = getAllParamValues(*params2, paramInfos2);
        if (actualValues != expectedValues)
        {
            throw std::runtime_error(
                "Setting the same parameter values through flush() and through the process "
                "function results in different reported values:" +
                formatMismatchingValues(actualValues, expectedValues, paramInfos2));
        }

        std::vector<uint8_t> actualState = saveState(stateExt2, plugin2->clapPlugin());
        host->handleCallbacksOnce();

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        if (actualState == expectedState)
        {
            return TestResult::success(testName, description);
        }
        return TestResult::failed(
            testName, description,
            "Setting the same parameter values on two instances (one via flush, one via process) "
            "resulted in different state files (" +
                std::to_string(expectedState.size()) + " bytes vs " +
                std::to_string(actualState.size()) + " bytes).");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testStateBufferedStreams(PluginLibrary &library,
                                                 const std::string &pluginId)
{
    const std::string testName = "state-buffered-streams";
    const std::string description =
        "Performs the same state and parameter reproducibility check, but the plugin is only "
        "allowed to read a small prime number of bytes at a time when reloading and resaving the "
        "state.";

    try
    {
        Prng prng = newPrng();
        auto host = std::make_shared<Host>();

        std::vector<uint8_t> expectedState;
        std::map<clap_id, double> expectedValues;
        ParamInfoMap paramInfos;

        // First instance: randomize parameters, then save the state unbuffered (ground truth).
        {
            auto plugin = library.createPlugin(pluginId, host);
            if (!plugin->init())
            {
                return TestResult::failed(testName, description, "Failed to initialize plugin");
            }

            auto audioConfig = AudioPortConfig::query(*plugin).value_or(AudioPortConfig{});
            auto params = ParamsExt::create(*plugin);
            if (!params)
            {
                return TestResult::skipped(testName, description,
                                           "The plugin does not implement the 'params' extension.");
            }
            const clap_plugin_state_t *stateExt =
                static_cast<const clap_plugin_state_t *>(plugin->getExtension(CLAP_EXT_STATE));
            if (!stateExt)
            {
                return TestResult::skipped(testName, description,
                                           "The plugin does not implement the 'state' extension.");
            }
            host->handleCallbacksOnce();

            paramInfos = params->info();
            EventList paramEventQueue;
            ParamFuzzer(paramInfos).randomizeParamsAt(prng, 0, paramEventQueue);
            std::vector<Event> paramEvents = paramEventQueue.events();

            AudioBuffers buffers(audioConfig, BUFFER_SIZE);
            ProcessingTest(*plugin, host, buffers)
                .runOnce(ProcessConfig{},
                         [&](ProcessData &processData)
                         {
                             for (const auto &event : paramEvents)
                             {
                                 processData.inputEvents().push(event);
                             }
                         });

            expectedValues = getAllParamValues(*params, paramInfos);
            expectedState = saveState(stateExt, plugin->clapPlugin());
            host->handleCallbacksOnce();
        }

        // Second instance: load with small chunked reads and re-save with small chunked writes.
        constexpr size_t kBufferedLoadMaxBytes = 17;
        constexpr size_t kBufferedSaveMaxBytes = 23;

        auto plugin2 = library.createPlugin(pluginId, host);
        if (!plugin2->init())
        {
            return TestResult::failed(testName, description,
                                      "Failed to initialize the second plugin instance");
        }
        auto params2 = ParamsExt::create(*plugin2);
        if (!params2)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        const clap_plugin_state_t *stateExt2 =
            static_cast<const clap_plugin_state_t *>(plugin2->getExtension(CLAP_EXT_STATE));
        if (!stateExt2)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'state' extension.");
        }
        host->handleCallbacksOnce();

        if (!loadState(stateExt2, plugin2->clapPlugin(), expectedState, kBufferedLoadMaxBytes))
        {
            throw std::runtime_error(
                "The plugin failed to load the state when only allowed to read " +
                std::to_string(kBufferedLoadMaxBytes) + " bytes at a time.");
        }
        host->handleCallbacksOnce();

        std::map<clap_id, double> actualValues = getAllParamValues(*params2, paramInfos);
        if (actualValues != expectedValues)
        {
            throw std::runtime_error(
                "After reloading the state with buffered reads, the plugin's parameter values do "
                "not match the saved values:" +
                formatMismatchingValues(actualValues, expectedValues, params2->info()));
        }

        std::vector<uint8_t> actualState =
            saveState(stateExt2, plugin2->clapPlugin(), kBufferedSaveMaxBytes);
        host->handleCallbacksOnce();

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        if (actualState == expectedState)
        {
            return TestResult::success(testName, description);
        }
        return TestResult::failed(testName, description,
                                  "Re-saving the loaded state with buffered streams resulted in a "
                                  "different state file (" +
                                      std::to_string(expectedState.size()) + " bytes expected, " +
                                      std::to_string(actualState.size()) + " bytes actual).");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

namespace
{
// A host-side context menu builder that validates every item the plugin adds while populating a
// menu. Non-thread-safe by design (as the CLAP builder is); used only on the calling thread.
struct MenuCollector
{
    std::optional<std::string> error;
    int submenuDepth = 0;
    size_t itemCount = 0;

    clap_context_menu_builder_t builder{};

    MenuCollector()
    {
        builder.ctx = this;
        builder.add_item = &MenuCollector::addItem;
        builder.supports = &MenuCollector::supports;
    }

    void reset()
    {
        error.reset();
        submenuDepth = 0;
        itemCount = 0;
    }

    void setError(const std::string &message)
    {
        if (!error)
        {
            error = message;
        }
    }

    static bool CLAP_ABI addItem(const clap_context_menu_builder_t *builder,
                                 clap_context_menu_item_kind_t itemKind, const void *itemData)
    {
        auto *self = static_cast<MenuCollector *>(builder->ctx);
        self->itemCount++;

        switch (itemKind)
        {
        case CLAP_CONTEXT_MENU_ITEM_ENTRY:
        {
            const auto *entry = static_cast<const clap_context_menu_entry_t *>(itemData);
            if (!entry || !entry->label)
            {
                self->setError("an ENTRY item has a null label");
                return false;
            }
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_CHECK_ENTRY:
        {
            const auto *entry = static_cast<const clap_context_menu_check_entry_t *>(itemData);
            if (!entry || !entry->label)
            {
                self->setError("a CHECK_ENTRY item has a null label");
                return false;
            }
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_SEPARATOR:
            break;
        case CLAP_CONTEXT_MENU_ITEM_BEGIN_SUBMENU:
        {
            const auto *submenu = static_cast<const clap_context_menu_submenu_t *>(itemData);
            if (!submenu || !submenu->label)
            {
                self->setError("a BEGIN_SUBMENU item has a null label");
                return false;
            }
            self->submenuDepth++;
            break;
        }
        case CLAP_CONTEXT_MENU_ITEM_END_SUBMENU:
            self->submenuDepth--;
            if (self->submenuDepth < 0)
            {
                self->setError("an END_SUBMENU item has no matching BEGIN_SUBMENU");
                return false;
            }
            break;
        case CLAP_CONTEXT_MENU_ITEM_TITLE:
        {
            const auto *title = static_cast<const clap_context_menu_item_title_t *>(itemData);
            if (!title || !title->title)
            {
                self->setError("a TITLE item has a null title");
                return false;
            }
            break;
        }
        default:
            self->setError("an unknown context menu item kind " + std::to_string(itemKind) +
                           " was added");
            return false;
        }
        return true;
    }

    static bool CLAP_ABI supports(const clap_context_menu_builder_t *,
                                  clap_context_menu_item_kind_t itemKind)
    {
        // We support all of the standard item kinds.
        return itemKind <= CLAP_CONTEXT_MENU_ITEM_TITLE;
    }
};
} // namespace

TestResult PluginTests::testContextMenu(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "context-menu";
    const std::string description =
        "If the plugin implements the 'context-menu' extension, populates its global and "
        "per-parameter context menus and checks that every menu item is well-formed: non-null "
        "labels and titles, balanced submenus, and known item kinds.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *menu = static_cast<const clap_plugin_context_menu_t *>(
            plugin->getExtension(CLAP_EXT_CONTEXT_MENU));
        if (!menu)
        {
            menu = static_cast<const clap_plugin_context_menu_t *>(
                plugin->getExtension(CLAP_EXT_CONTEXT_MENU_COMPAT));
        }
        if (!menu || !menu->populate)
        {
            return TestResult::skipped(
                testName, description,
                "The plugin does not implement the 'context-menu' extension.");
        }
        host->handleCallbacksOnce();

        MenuCollector collector;

        // Populate the menu for a target and validate the items the plugin added. Returns an error
        // description if the menu was malformed.
        auto validate = [&](const clap_context_menu_target_t *target,
                            const std::string &targetName) -> std::optional<std::string>
        {
            collector.reset();
            menu->populate(plugin->clapPlugin(), target, &collector.builder);
            if (collector.error)
            {
                return "The " + targetName + " is malformed: " + *collector.error + ".";
            }
            if (collector.submenuDepth != 0)
            {
                return "The " + targetName + " has " + std::to_string(collector.submenuDepth) +
                       " unclosed submenu(s).";
            }
            return std::nullopt;
        };

        // A null target means the global context.
        if (auto err = validate(nullptr, "global context menu"))
        {
            return TestResult::failed(testName, description, *err);
        }

        // And a per-parameter menu for each parameter, if the plugin has any.
        if (auto params = ParamsExt::create(*plugin))
        {
            for (const auto &entry : params->info())
            {
                clap_context_menu_target_t target{CLAP_CONTEXT_MENU_TARGET_KIND_PARAM, entry.first};
                if (auto err =
                        validate(&target, "context menu for parameter '" + entry.second.name + "'"))
                {
                    return TestResult::failed(testName, description, *err);
                }
            }
        }

        host->handleCallbacksOnce();
        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testLatency(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "latency";
    const std::string description =
        "If the plugin implements the 'latency' extension, checks that its reported latency is "
        "readable while the plugin is active and stable across reads.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *latency =
            static_cast<const clap_plugin_latency_t *>(plugin->getExtension(CLAP_EXT_LATENCY));
        if (!latency || !latency->get)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'latency' extension.");
        }
        host->handleCallbacksOnce();

        // 'latency.get()' may only be called while the plugin is active.
        if (!plugin->activate(44100.0, 1, BUFFER_SIZE))
        {
            return TestResult::failed(testName, description, "Failed to activate plugin");
        }
        uint32_t first = latency->get(plugin->clapPlugin());
        uint32_t second = latency->get(plugin->clapPlugin());
        plugin->deactivate();

        if (first != second)
        {
            return TestResult::failed(testName, description,
                                      "'latency.get()' returned " + std::to_string(first) +
                                          " then " + std::to_string(second) +
                                          " within one activation; latency must be stable while "
                                          "active and only change during activation.");
        }
        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description,
                                   "Reported latency: " + std::to_string(first) + " samples");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testTail(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "tail";
    const std::string description =
        "If the plugin implements the 'tail' extension, checks that its reported tail length is "
        "readable and stable.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *tail =
            static_cast<const clap_plugin_tail_t *>(plugin->getExtension(CLAP_EXT_TAIL));
        if (!tail || !tail->get)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'tail' extension.");
        }
        host->handleCallbacksOnce();

        // Tail is reported in samples, which depends on the sample rate, so query it while the
        // plugin is active (and knows the rate).
        if (!plugin->activate(44100.0, 1, BUFFER_SIZE))
        {
            return TestResult::failed(testName, description, "Failed to activate plugin");
        }
        uint32_t first = tail->get(plugin->clapPlugin());
        uint32_t second = tail->get(plugin->clapPlugin());
        plugin->deactivate();
        if (first != second)
        {
            return TestResult::failed(testName, description,
                                      "'tail.get()' returned " + std::to_string(first) + " then " +
                                          std::to_string(second) +
                                          " on consecutive reads; the tail should be stable.");
        }
        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        std::string reported = first >= static_cast<uint32_t>(INT32_MAX)
                                   ? "infinite"
                                   : std::to_string(first) + " samples";
        return TestResult::success(testName, description, "Reported tail: " + reported);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testVoiceInfo(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "voice-info";
    const std::string description =
        "If the plugin implements the 'voice-info' extension, checks that it reports "
        "1 <= voice_count <= voice_capacity while the plugin is active.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *voiceInfo = static_cast<const clap_plugin_voice_info_t *>(
            plugin->getExtension(CLAP_EXT_VOICE_INFO));
        if (!voiceInfo || !voiceInfo->get)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'voice-info' extension.");
        }
        host->handleCallbacksOnce();

        // 'voice_info.get()' may only be called while the plugin is active.
        if (!plugin->activate(44100.0, 1, BUFFER_SIZE))
        {
            return TestResult::failed(testName, description, "Failed to activate plugin");
        }
        clap_voice_info_t info = {};
        bool ok = voiceInfo->get(plugin->clapPlugin(), &info);
        plugin->deactivate();

        if (!ok)
        {
            return TestResult::failed(testName, description,
                                      "'voice_info.get()' returned false while the plugin was "
                                      "active.");
        }
        if (info.voice_count < 1 || info.voice_count > info.voice_capacity)
        {
            return TestResult::failed(
                testName, description,
                "The plugin reported voice_count=" + std::to_string(info.voice_count) +
                    " and voice_capacity=" + std::to_string(info.voice_capacity) +
                    "; the spec requires 1 <= voice_count <= voice_capacity.");
        }
        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description,
                                   "voice_count=" + std::to_string(info.voice_count) +
                                       ", voice_capacity=" + std::to_string(info.voice_capacity));
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testNoteName(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "note-name";
    const std::string description =
        "If the plugin implements the 'note-name' extension, checks that every declared note name "
        "can be queried and has valid key, channel, and port ranges.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *noteName =
            static_cast<const clap_plugin_note_name_t *>(plugin->getExtension(CLAP_EXT_NOTE_NAME));
        if (!noteName || !noteName->count || !noteName->get)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'note-name' extension.");
        }
        host->handleCallbacksOnce();

        uint32_t count = noteName->count(plugin->clapPlugin());
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_note_name_t name = {};
            if (!noteName->get(plugin->clapPlugin(), i, &name))
            {
                return TestResult::failed(testName, description,
                                          "'note_name.get(" + std::to_string(i) +
                                              ")' returned false (" + std::to_string(count) +
                                              " note names were reported).");
            }
            if (name.key < -1 || name.key > 127)
            {
                return TestResult::failed(testName, description,
                                          "Note name " + std::to_string(i) + " has key " +
                                              std::to_string(name.key) +
                                              " (must be -1 for every key, or 0..127).");
            }
            if (name.channel < -1 || name.channel > 15)
            {
                return TestResult::failed(testName, description,
                                          "Note name " + std::to_string(i) + " has channel " +
                                              std::to_string(name.channel) +
                                              " (must be -1 for every channel, or 0..15).");
            }
            if (name.port < -1)
            {
                return TestResult::failed(
                    testName, description,
                    "Note name " + std::to_string(i) + " has port " + std::to_string(name.port) +
                        " (must be -1 for every port, or a valid port index).");
            }
        }
        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description, std::to_string(count) + " note name(s)");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testRender(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "render";
    const std::string description =
        "If the plugin implements the 'render' extension, checks the render mode setters: the "
        "realtime mode is accepted and a plugin with a hard realtime "
        "requirement rejects the offline mode.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *render =
            static_cast<const clap_plugin_render_t *>(plugin->getExtension(CLAP_EXT_RENDER));
        if (!render || !render->set || !render->has_hard_realtime_requirement)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'render' extension.");
        }
        host->handleCallbacksOnce();

        const clap_plugin_t *cp = plugin->clapPlugin();
        bool hardRealtime = render->has_hard_realtime_requirement(cp);

        if (!render->set(cp, CLAP_RENDER_REALTIME))
        {
            return TestResult::failed(testName, description,
                                      "'render.set(CLAP_RENDER_REALTIME)' returned false; the "
                                      "default realtime mode should always be applicable.");
        }

        bool offlineApplied = render->set(cp, CLAP_RENDER_OFFLINE);
        if (hardRealtime && offlineApplied)
        {
            return TestResult::failed(testName, description,
                                      "The plugin reports a hard realtime requirement but accepted "
                                      "the offline render mode.");
        }

        // Leave the plugin back in the default realtime mode.
        render->set(cp, CLAP_RENDER_REALTIME);

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testParamDefaults(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "param-defaults";
    const std::string description =
        "Checks that a freshly created plugin reports each parameter's declared default value "
        "before any state is loaded.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        auto params = ParamsExt::create(*plugin);
        if (!params)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        host->handleCallbacksOnce();

        ParamInfoMap paramInfos = params->info();
        std::vector<std::string> mismatches;
        for (const auto &[id, info] : paramInfos)
        {
            double value = params->getValue(id);
            if (value != info.defaultValue)
            {
                mismatches.push_back("parameter '" + info.name + "': default " +
                                     formatDouble(info.defaultValue) + ", actual " +
                                     formatDouble(value) + " (diff " +
                                     formatDouble(value - info.defaultValue, 6) + ")");
            }
        }

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        if (!mismatches.empty())
        {
            return TestResult::failed(testName, description,
                                      "A freshly created plugin's parameter values do not match "
                                      "their declared defaults:" +
                                          formatTruncatedList(mismatches));
        }
        return TestResult::success(testName, description,
                                   std::to_string(paramInfos.size()) +
                                       " parameter(s) at their declared defaults");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testParamInfoStable(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "param-info-stable";
    const std::string description =
        "Checks that the plugin's parameter information (ids, cookies, ranges, flags) is identical "
        "across repeated queries.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        auto params = ParamsExt::create(*plugin);
        if (!params)
        {
            return TestResult::skipped(testName, description,
                                       "The plugin does not implement the 'params' extension.");
        }
        host->handleCallbacksOnce();

        ParamInfoMap first = params->info();
        ParamInfoMap second = params->info();
        if (first.size() != second.size())
        {
            return TestResult::failed(
                testName, description,
                "The parameter count changed between two queries: " + std::to_string(first.size()) +
                    " then " + std::to_string(second.size()) + ".");
        }
        for (const auto &[id, a] : first)
        {
            auto it = second.find(id);
            if (it == second.end())
            {
                return TestResult::failed(testName, description,
                                          "Parameter id " + std::to_string(id) +
                                              " was present in the first query but missing in the "
                                              "second.");
            }
            const ParamInfo &b = it->second;
            if (a.name != b.name || a.cookie != b.cookie || a.minValue != b.minValue ||
                a.maxValue != b.maxValue || a.defaultValue != b.defaultValue || a.flags != b.flags)
            {
                return TestResult::failed(
                    testName, description,
                    "Parameter '" + a.name + "' (id " + std::to_string(id) +
                        ") reported different info across two queries; "
                        "parameter info (especially cookies) must be stable.");
            }
        }

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description,
                                   std::to_string(first.size()) + " parameter(s) with stable info");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testAudioPortsConfig(PluginLibrary &library, const std::string &pluginId)
{
    const std::string testName = "audio-ports-config";
    const std::string description =
        "If the plugin implements the 'audio-ports-config' extension, enumerates its port "
        "configurations, selects each one, and checks that the plugin's audio ports then match the "
        "selected configuration.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const auto *config = static_cast<const clap_plugin_audio_ports_config_t *>(
            plugin->getExtension(CLAP_EXT_AUDIO_PORTS_CONFIG));
        if (!config || !config->count || !config->get || !config->select)
        {
            return TestResult::skipped(
                testName, description,
                "The plugin does not implement the 'audio-ports-config' extension.");
        }
        const auto *ports = static_cast<const clap_plugin_audio_ports_t *>(
            plugin->getExtension(CLAP_EXT_AUDIO_PORTS));
        host->handleCallbacksOnce();

        const clap_plugin_t *cp = plugin->clapPlugin();

        uint32_t count = config->count(cp);
        std::vector<clap_audio_ports_config_t> configs;
        std::set<clap_id> seenIds;
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_audio_ports_config_t c = {};
            if (!config->get(cp, i, &c))
            {
                return TestResult::failed(testName, description,
                                          "'audio_ports_config.get(" + std::to_string(i) +
                                              ")' returned false (" + std::to_string(count) +
                                              " configs reported).");
            }
            if (!seenIds.insert(c.id).second)
            {
                return TestResult::failed(testName, description,
                                          "Two audio port configurations share the id " +
                                              std::to_string(c.id) + ".");
            }
            configs.push_back(c);
        }

        // The plugin is inactive here, so 'select()' is allowed.
        for (const auto &c : configs)
        {
            std::string configName(c.name, ::strnlen(c.name, sizeof(c.name)));
            if (!config->select(cp, c.id))
            {
                return TestResult::failed(testName, description,
                                          "'audio_ports_config.select()' returned false for the "
                                          "advertised configuration '" +
                                              configName + "'.");
            }
            if (!ports || !ports->count)
            {
                continue;
            }
            uint32_t numInputs = ports->count(cp, true);
            uint32_t numOutputs = ports->count(cp, false);
            if (numInputs != c.input_port_count || numOutputs != c.output_port_count)
            {
                return TestResult::failed(testName, description,
                                          "After selecting configuration '" + configName +
                                              "', the plugin reports " + std::to_string(numInputs) +
                                              " input and " + std::to_string(numOutputs) +
                                              " output ports, but the configuration declares " +
                                              std::to_string(c.input_port_count) + " and " +
                                              std::to_string(c.output_port_count) + ".");
            }

            // The main port must be at index 0 (see audio-ports.h). Verify its channel count.
            auto checkMain = [&](bool isInput, bool hasMain, uint32_t mainChannels,
                                 const std::string &which) -> std::optional<std::string>
            {
                if (!hasMain || !ports->get)
                {
                    return std::nullopt;
                }
                clap_audio_port_info_t info = {};
                if (!ports->get(cp, 0, isInput, &info))
                {
                    return "after selecting configuration '" + configName +
                           "', the plugin's main " + which +
                           " port (index 0) could not be queried.";
                }
                if (info.channel_count != mainChannels)
                {
                    return "after selecting configuration '" + configName +
                           "', the plugin's main " + which + " port has " +
                           std::to_string(info.channel_count) +
                           " channels, but the configuration declares " +
                           std::to_string(mainChannels) + ".";
                }
                return std::nullopt;
            };
            if (auto err = checkMain(true, c.has_main_input, c.main_input_channel_count, "input"))
            {
                return TestResult::failed(testName, description, *err);
            }
            if (auto err =
                    checkMain(false, c.has_main_output, c.main_output_channel_count, "output"))
            {
                return TestResult::failed(testName, description, *err);
            }
        }

        if (auto err = host->getCallbackError())
        {
            return TestResult::failed(testName, description, *err);
        }
        return TestResult::success(testName, description,
                                   std::to_string(count) + " audio port configuration(s)");
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

} // namespace clap_validator
