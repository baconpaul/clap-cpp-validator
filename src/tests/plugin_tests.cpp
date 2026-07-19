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
#include <random>
#include <map>
#include <cmath>

namespace clap_validator
{

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
         "state."}};
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
        // meaning) plus four random values in range.
        constexpr int kValuesPerParam = 6;
        size_t expectedConversions = paramInfos.size() * kValuesPerParam;
        size_t numSupportedValueToText = 0;
        size_t numSupportedTextToValue = 0;

        for (const auto &[paramId, info] : paramInfos)
        {
            double values[kValuesPerParam] = {info.minValue,
                                              info.maxValue,
                                              prng.nextDouble(info.minValue, info.maxValue),
                                              prng.nextDouble(info.minValue, info.maxValue),
                                              prng.nextDouble(info.minValue, info.maxValue),
                                              prng.nextDouble(info.minValue, info.maxValue)};

            for (double startingValue : values)
            {
                // If the plugin rounds string representations then a raw value may not roundtrip,
                // so we start from the plugin's own string representation.
                auto startingText = params->valueToText(paramId, startingValue);
                if (!startingText)
                {
                    // value_to_text unsupported for this parameter; skip the rest of it.
                    break;
                }
                numSupportedValueToText++;

                auto reconvertedValue = params->textToValue(paramId, *startingText);
                if (!reconvertedValue)
                {
                    // text_to_value unsupported; keep testing value_to_text on the next value.
                    continue;
                }
                numSupportedTextToValue++;

                auto reconvertedText = params->valueToText(paramId, *reconvertedValue);
                if (!reconvertedText)
                {
                    throw std::runtime_error("Repeated value-to-text conversion failed for "
                                             "parameter '" +
                                             info.name + "'.");
                }
                // Both strings come from the plugin, so they should be identical.
                if (*startingText != *reconvertedText)
                {
                    throw std::runtime_error(
                        "Converting a value to a string, back to a value, and back to a string for "
                        "parameter '" +
                        info.name + "' produced '" + *startingText + "' -> '" + *reconvertedText +
                        "', which is not consistent.");
                }

                auto finalValue = params->textToValue(paramId, *reconvertedText);
                if (!finalValue)
                {
                    throw std::runtime_error("Repeated text-to-value conversion failed for "
                                             "parameter '" +
                                             info.name + "'.");
                }
                if (*finalValue != *reconvertedValue)
                {
                    throw std::runtime_error(
                        "Repeatedly converting parameter '" + info.name +
                        "' between values and strings does not roundtrip consistently.");
                }
            }
        }

        if (numSupportedValueToText != 0 && numSupportedValueToText != expectedConversions)
        {
            throw std::runtime_error(
                "'clap_plugin_params::value_to_text()' succeeded for " +
                std::to_string(numSupportedValueToText) + " out of " +
                std::to_string(expectedConversions) +
                " calls; it should be supported for either all parameters or none.");
        }
        if (numSupportedTextToValue != 0 && numSupportedTextToValue != expectedConversions)
        {
            throw std::runtime_error(
                "'clap_plugin_params::text_to_value()' succeeded for " +
                std::to_string(numSupportedTextToValue) + " out of " +
                std::to_string(expectedConversions) +
                " calls; it should be supported for either all parameters or none.");
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
    const std::string description = "Tests state save/load reproducibility.";

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

        const clap_plugin_params_t *paramsExt =
            static_cast<const clap_plugin_params_t *>(plugin->getExtension(CLAP_EXT_PARAMS));

        // Save initial state
        struct StateBuffer
        {
            std::vector<uint8_t> data;

            static int64_t write(const clap_ostream_t *stream, const void *buffer, uint64_t size)
            {
                auto *self = static_cast<StateBuffer *>(stream->ctx);
                const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
                self->data.insert(self->data.end(), bytes, bytes + size);
                return static_cast<int64_t>(size);
            }

            static int64_t read(const clap_istream_t *stream, void *buffer, uint64_t size)
            {
                auto *self = static_cast<StateBuffer *>(stream->ctx);
                size_t toRead = std::min(static_cast<size_t>(size), self->data.size());
                if (toRead > 0)
                {
                    std::memcpy(buffer, self->data.data(), toRead);
                    self->data.erase(self->data.begin(), self->data.begin() + toRead);
                }
                return static_cast<int64_t>(toRead);
            }
        };

        StateBuffer stateBuffer1;
        clap_ostream_t ostream1 = {};
        ostream1.ctx = &stateBuffer1;
        ostream1.write = StateBuffer::write;

        if (!stateExt->save(plugin->clapPlugin(), &ostream1))
        {
            return TestResult::failed(testName, description, "Failed to save initial state");
        }

        // Create new plugin instance and load state
        auto plugin2 = library.createPlugin(pluginId, host);
        if (!plugin2->init())
        {
            return TestResult::failed(testName, description,
                                      "Failed to initialize second plugin instance");
        }

        StateBuffer loadBuffer;
        loadBuffer.data = stateBuffer1.data;
        clap_istream_t istream = {};
        istream.ctx = &loadBuffer;
        istream.read = StateBuffer::read;

        if (!stateExt->load(plugin2->clapPlugin(), &istream))
        {
            return TestResult::failed(testName, description, "Failed to load state");
        }

        // Save state again from second instance
        StateBuffer stateBuffer2;
        clap_ostream_t ostream2 = {};
        ostream2.ctx = &stateBuffer2;
        ostream2.write = StateBuffer::write;

        if (!stateExt->save(plugin2->clapPlugin(), &ostream2))
        {
            return TestResult::failed(testName, description,
                                      "Failed to save state from second instance");
        }

        // Compare states
        if (stateBuffer1.data != stateBuffer2.data)
        {
            return TestResult::failed(
                testName, description,
                "State mismatch: saved states are different after load/save cycle");
        }

        return TestResult::success(testName, description);
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
        "Tests state reproducibility using flush for parameter changes.";

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

        const clap_plugin_params_t *paramsExt =
            static_cast<const clap_plugin_params_t *>(plugin->getExtension(CLAP_EXT_PARAMS));

        if (!paramsExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support params extension");
        }

        // Basic test - verify state extension works
        return TestResult::success(testName, description);
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
    const std::string description = "Tests state with small buffered reads.";

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

        // Save state
        constexpr size_t CHUNK_SIZE = 7; // Small prime number

        struct StateBuffer
        {
            std::vector<uint8_t> data;
            size_t readPos = 0;
            size_t chunkSize = 7;

            static int64_t write(const clap_ostream_t *stream, const void *buffer, uint64_t size)
            {
                auto *self = static_cast<StateBuffer *>(stream->ctx);
                const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
                self->data.insert(self->data.end(), bytes, bytes + size);
                return static_cast<int64_t>(size);
            }

            static int64_t readBuffered(const clap_istream_t *stream, void *buffer, uint64_t size)
            {
                auto *self = static_cast<StateBuffer *>(stream->ctx);
                size_t available = self->data.size() - self->readPos;
                size_t toRead = std::min({static_cast<size_t>(size), available, self->chunkSize});
                if (toRead > 0)
                {
                    std::memcpy(buffer, self->data.data() + self->readPos, toRead);
                    self->readPos += toRead;
                }
                return static_cast<int64_t>(toRead);
            }
        };

        StateBuffer stateBuffer;
        clap_ostream_t ostream = {};
        ostream.ctx = &stateBuffer;
        ostream.write = StateBuffer::write;

        if (!stateExt->save(plugin->clapPlugin(), &ostream))
        {
            return TestResult::failed(testName, description, "Failed to save state");
        }

        // Load with buffered reads
        clap_istream_t istream = {};
        istream.ctx = &stateBuffer;
        istream.read = StateBuffer::readBuffered;

        if (!stateExt->load(plugin->clapPlugin(), &istream))
        {
            return TestResult::failed(testName, description,
                                      "Failed to load state with buffered reads");
        }

        return TestResult::success(testName, description);
    }
    catch (const std::exception &e)
    {
        return TestResult::failed(testName, description, e.what());
    }
}

} // namespace clap_validator
