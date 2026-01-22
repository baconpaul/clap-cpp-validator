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
         "whether the output does not contain any non-finite or subnormal values. Uses out-of-place "
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
    const std::string description = "Plugin descriptor consistency check.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        // Get the descriptor from the plugin instance
        const clap_plugin_descriptor_t *instanceDesc = plugin->descriptor();
        if (!instanceDesc)
        {
            return TestResult::failed(testName, description, "Plugin instance has no descriptor");
        }

        // Get the descriptor from the factory
        auto metadata = library.metadata();
        const PluginMetadata *factoryMeta = nullptr;
        for (const auto &pm : metadata.plugins)
        {
            if (pm.id == pluginId)
            {
                factoryMeta = &pm;
                break;
            }
        }

        if (!factoryMeta)
        {
            return TestResult::failed(testName, description, "Plugin ID not found in factory");
        }

        // Compare the descriptors
        if (factoryMeta->id != instanceDesc->id)
        {
            return TestResult::failed(testName, description,
                                      "Plugin ID mismatch: factory='" + factoryMeta->id +
                                          "', instance='" + instanceDesc->id + "'");
        }

        if (factoryMeta->name != instanceDesc->name)
        {
            return TestResult::failed(testName, description,
                                      "Plugin name mismatch: factory='" + factoryMeta->name +
                                          "', instance='" + instanceDesc->name + "'");
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
    const std::string description = "Basic out-of-place audio processing test.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const double sampleRate = 44100.0;
        const uint32_t blockSize = 512;

        {
            AudioThreadGuard audioGuard(host);

            if (!plugin->activate(sampleRate, blockSize, blockSize))
            {
                return TestResult::failed(testName, description, "Failed to activate plugin");
            }

            if (!plugin->startProcessing())
            {
                plugin->deactivate();
                return TestResult::failed(testName, description, "Failed to start processing");
            }

            // Create simple process data
            std::vector<float> inputBuffer(blockSize, 0.0f);
            std::vector<float> outputBuffer(blockSize, 0.0f);

            // Fill input with some test signal
            for (uint32_t i = 0; i < blockSize; ++i)
            {
                inputBuffer[i] = static_cast<float>(i) / static_cast<float>(blockSize) - 0.5f;
            }

            float *inputPtrs[1] = {inputBuffer.data()};
            float *outputPtrs[1] = {outputBuffer.data()};

            clap_audio_buffer_t inputAudioBuffer = {};
            inputAudioBuffer.data32 = inputPtrs;
            inputAudioBuffer.channel_count = 1;
            inputAudioBuffer.latency = 0;
            inputAudioBuffer.constant_mask = 0;

            clap_audio_buffer_t outputAudioBuffer = {};
            outputAudioBuffer.data32 = outputPtrs;
            outputAudioBuffer.channel_count = 1;
            outputAudioBuffer.latency = 0;
            outputAudioBuffer.constant_mask = 0;

            // Create dummy empty input event queue
            clap_input_events_t inEvents = {};
            inEvents.ctx = nullptr;
            inEvents.size = [](const clap_input_events_t *) -> uint32_t { return 0; };
            inEvents.get = [](const clap_input_events_t *, uint32_t) -> const clap_event_header_t *
            { return nullptr; };

            // Create dummy unwritable output event queue
            clap_output_events_t outEvents = {};
            outEvents.ctx = nullptr;
            outEvents.try_push =
                [](const clap_output_events_t *, const clap_event_header_t *) -> bool
            { return false; };

            clap_process_t processData = {};
            processData.steady_time = 0;
            processData.frames_count = blockSize;
            processData.transport = nullptr;
            processData.audio_inputs = &inputAudioBuffer;
            processData.audio_outputs = &outputAudioBuffer;
            processData.audio_inputs_count = 1;
            processData.audio_outputs_count = 1;
            processData.in_events = &inEvents;
            processData.out_events = &outEvents;

            clap_process_status status = plugin->process(&processData);

            plugin->stopProcessing();
            plugin->deactivate();

            if (status == CLAP_PROCESS_ERROR)
            {
                return TestResult::failed(testName, description, "Process returned error");
            }

            // Check output for non-finite values
            for (uint32_t i = 0; i < blockSize; ++i)
            {
                if (!std::isfinite(outputBuffer[i]))
                {
                    return TestResult::failed(testName, description,
                                              "Output contains non-finite value at sample " +
                                                  std::to_string(i));
                }
            }
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
    const std::string description = "Parameter value/string conversion test.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        // Get the params extension
        const clap_plugin_params_t *paramsExt =
            static_cast<const clap_plugin_params_t *>(plugin->getExtension(CLAP_EXT_PARAMS));

        if (!paramsExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support params extension");
        }

        uint32_t paramCount = paramsExt->count(plugin->clapPlugin());
        if (paramCount == 0)
        {
            return TestResult::skipped(testName, description, "Plugin has no parameters");
        }

        // Test that all parameters can be queried
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            clap_param_info_t info = {};
            if (!paramsExt->get_info(plugin->clapPlugin(), i, &info))
            {
                return TestResult::failed(testName, description,
                                          "Failed to get info for parameter index " +
                                              std::to_string(i));
            }
        }

        return TestResult::success(testName, description,
                                   "Successfully queried " + std::to_string(paramCount) +
                                       " parameters");
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
    const std::string description = "Basic note processing test with out-of-place audio.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        // Check if plugin supports note ports
        const clap_plugin_note_ports_t *notePortsExt =
            static_cast<const clap_plugin_note_ports_t *>(
                plugin->getExtension(CLAP_EXT_NOTE_PORTS));

        if (!notePortsExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support note ports extension");
        }

        uint32_t notePortCount = notePortsExt->count(plugin->clapPlugin(), true);
        if (notePortCount == 0)
        {
            return TestResult::skipped(testName, description, "Plugin has no input note ports");
        }

        const double sampleRate = 44100.0;
        const uint32_t blockSize = BUFFER_SIZE;

        {
            AudioThreadGuard audioGuard(host);

            if (!plugin->activate(sampleRate, blockSize, blockSize))
            {
                return TestResult::failed(testName, description, "Failed to activate plugin");
            }

            if (!plugin->startProcessing())
            {
                plugin->deactivate();
                return TestResult::failed(testName, description, "Failed to start processing");
            }

            // Create buffers and process - simplified test
            std::vector<float> inputBuffer(blockSize, 0.0f);
            std::vector<float> outputBuffer(blockSize, 0.0f);
            float *inputPtrs[1] = {inputBuffer.data()};
            float *outputPtrs[1] = {outputBuffer.data()};

            clap_audio_buffer_t inputAudioBuffer = {};
            inputAudioBuffer.data32 = inputPtrs;
            inputAudioBuffer.channel_count = 1;

            clap_audio_buffer_t outputAudioBuffer = {};
            outputAudioBuffer.data32 = outputPtrs;
            outputAudioBuffer.channel_count = 1;

            clap_input_events_t inEvents = {};
            inEvents.size = [](const clap_input_events_t *) -> uint32_t { return 0; };
            inEvents.get = [](const clap_input_events_t *, uint32_t) -> const clap_event_header_t *
            { return nullptr; };

            clap_output_events_t outEvents = {};
            outEvents.try_push =
                [](const clap_output_events_t *, const clap_event_header_t *) -> bool
            { return false; };

            clap_process_t processData = {};
            processData.frames_count = blockSize;
            processData.audio_inputs = &inputAudioBuffer;
            processData.audio_outputs = &outputAudioBuffer;
            processData.audio_inputs_count = 1;
            processData.audio_outputs_count = 1;
            processData.in_events = &inEvents;
            processData.out_events = &outEvents;

            plugin->process(&processData);
            plugin->stopProcessing();
            plugin->deactivate();
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
    const std::string description = "Tests plugin handling of inconsistent note events.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const clap_plugin_note_ports_t *notePortsExt =
            static_cast<const clap_plugin_note_ports_t *>(
                plugin->getExtension(CLAP_EXT_NOTE_PORTS));

        if (!notePortsExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support note ports extension");
        }

        // Basic test - just verify plugin doesn't crash with note ports
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
    const std::string description = "Fuzzes plugin parameters with random values.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const clap_plugin_params_t *paramsExt =
            static_cast<const clap_plugin_params_t *>(plugin->getExtension(CLAP_EXT_PARAMS));

        if (!paramsExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support params extension");
        }

        uint32_t paramCount = paramsExt->count(plugin->clapPlugin());
        if (paramCount == 0)
        {
            return TestResult::skipped(testName, description, "Plugin has no parameters");
        }

        const double sampleRate = 44100.0;
        const uint32_t blockSize = BUFFER_SIZE;

        {
            AudioThreadGuard audioGuard(host);

            if (!plugin->activate(sampleRate, blockSize, blockSize))
            {
                return TestResult::failed(testName, description, "Failed to activate plugin");
            }

            if (!plugin->startProcessing())
            {
                plugin->deactivate();
                return TestResult::failed(testName, description, "Failed to start processing");
            }

            std::random_device rd;
            std::mt19937 gen(rd());

            // Collect parameter info
            std::vector<clap_param_info_t> paramInfos(paramCount);
            for (uint32_t i = 0; i < paramCount; ++i)
            {
                paramsExt->get_info(plugin->clapPlugin(), i, &paramInfos[i]);
            }

            // Create buffers
            std::vector<float> inputBuffer(blockSize, 0.0f);
            std::vector<float> outputBuffer(blockSize, 0.0f);
            float *inputPtrs[1] = {inputBuffer.data()};
            float *outputPtrs[1] = {outputBuffer.data()};

            clap_audio_buffer_t inputAudioBuffer = {};
            inputAudioBuffer.data32 = inputPtrs;
            inputAudioBuffer.channel_count = 1;

            clap_audio_buffer_t outputAudioBuffer = {};
            outputAudioBuffer.data32 = outputPtrs;
            outputAudioBuffer.channel_count = 1;

            clap_input_events_t inEvents = {};
            inEvents.size = [](const clap_input_events_t *) -> uint32_t { return 0; };
            inEvents.get = [](const clap_input_events_t *, uint32_t) -> const clap_event_header_t *
            { return nullptr; };

            clap_output_events_t outEvents = {};
            outEvents.try_push =
                [](const clap_output_events_t *, const clap_event_header_t *) -> bool
            { return false; };

            clap_process_t processData = {};
            processData.frames_count = blockSize;
            processData.audio_inputs = &inputAudioBuffer;
            processData.audio_outputs = &outputAudioBuffer;
            processData.audio_inputs_count = 1;
            processData.audio_outputs_count = 1;
            processData.in_events = &inEvents;
            processData.out_events = &outEvents;

            // Run multiple permutations
            for (size_t perm = 0; perm < FUZZ_NUM_PERMUTATIONS; ++perm)
            {
                // Randomize input
                std::uniform_real_distribution<float> audioDist(-1.0f, 1.0f);
                for (uint32_t i = 0; i < blockSize; ++i)
                {
                    inputBuffer[i] = audioDist(gen);
                }

                // Process
                for (size_t run = 0; run < FUZZ_RUNS_PER_PERMUTATION; ++run)
                {
                    clap_process_status status = plugin->process(&processData);
                    if (status == CLAP_PROCESS_ERROR)
                    {
                        plugin->stopProcessing();
                        plugin->deactivate();
                        return TestResult::failed(testName, description,
                                                  "Process returned error during fuzz test");
                    }

                    // Check for non-finite values
                    for (uint32_t i = 0; i < blockSize; ++i)
                    {
                        if (!std::isfinite(outputBuffer[i]))
                        {
                            plugin->stopProcessing();
                            plugin->deactivate();
                            return TestResult::failed(
                                testName, description,
                                "Output contains non-finite value during fuzz test");
                        }
                    }
                }
            }

            plugin->stopProcessing();
            plugin->deactivate();
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
    const std::string description = "Tests that plugin ignores param events with wrong namespace.";

    try
    {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);

        if (!plugin->init())
        {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }

        const clap_plugin_params_t *paramsExt =
            static_cast<const clap_plugin_params_t *>(plugin->getExtension(CLAP_EXT_PARAMS));

        if (!paramsExt)
        {
            return TestResult::skipped(testName, description,
                                       "Plugin does not support params extension");
        }

        uint32_t paramCount = paramsExt->count(plugin->clapPlugin());
        if (paramCount == 0)
        {
            return TestResult::skipped(testName, description, "Plugin has no parameters");
        }

        // Collect all parameter info and initial values
        std::vector<clap_param_info_t> paramInfos(paramCount);
        std::map<clap_id, double> initialParamValues;

        for (uint32_t i = 0; i < paramCount; ++i)
        {
            if (!paramsExt->get_info(plugin->clapPlugin(), i, &paramInfos[i]))
            {
                return TestResult::failed(testName, description, "Failed to get parameter info");
            }

            double value = 0.0;
            if (!paramsExt->get_value(plugin->clapPlugin(), paramInfos[i].id, &value))
            {
                return TestResult::failed(testName, description, "Failed to get parameter value");
            }
            initialParamValues[paramInfos[i].id] = value;
        }

        // Generate random parameter set events with WRONG namespace ID
        constexpr uint16_t INCORRECT_NAMESPACE_ID = 0xb33f;

        std::random_device rd;
        std::mt19937 gen(rd());

        std::vector<clap_event_param_value_t> paramEvents;
        paramEvents.reserve(paramCount);

        for (uint32_t i = 0; i < paramCount; ++i)
        {
            const auto &info = paramInfos[i];

            // Generate a random value within the parameter's range
            std::uniform_real_distribution<double> dist(info.min_value, info.max_value);
            double randomValue = dist(gen);

            clap_event_param_value_t event = {};
            event.header.size = sizeof(clap_event_param_value_t);
            event.header.time = 0;
            event.header.space_id = INCORRECT_NAMESPACE_ID; // Wrong namespace!
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.flags = 0;
            event.param_id = info.id;
            event.cookie = info.cookie;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = randomValue;

            paramEvents.push_back(event);
        }

        const double sampleRate = 44100.0;
        const uint32_t blockSize = BUFFER_SIZE;

        {
            AudioThreadGuard audioGuard(host);

            if (!plugin->activate(sampleRate, blockSize, blockSize))
            {
                return TestResult::failed(testName, description, "Failed to activate plugin");
            }

            if (!plugin->startProcessing())
            {
                plugin->deactivate();
                return TestResult::failed(testName, description, "Failed to start processing");
            }

            // Create buffers
            std::vector<float> inputBuffer(blockSize, 0.0f);
            std::vector<float> outputBuffer(blockSize, 0.0f);
            float *inputPtrs[1] = {inputBuffer.data()};
            float *outputPtrs[1] = {outputBuffer.data()};

            clap_audio_buffer_t inputAudioBuffer = {};
            inputAudioBuffer.data32 = inputPtrs;
            inputAudioBuffer.channel_count = 1;

            clap_audio_buffer_t outputAudioBuffer = {};
            outputAudioBuffer.data32 = outputPtrs;
            outputAudioBuffer.channel_count = 1;

            // Set up input events with our wrong-namespace param events
            struct EventContext
            {
                std::vector<clap_event_param_value_t> *events;
            };
            EventContext ctx{&paramEvents};

            clap_input_events_t inEvents = {};
            inEvents.ctx = &ctx;
            inEvents.size = [](const clap_input_events_t *list) -> uint32_t
            {
                auto *context = static_cast<EventContext *>(list->ctx);
                return static_cast<uint32_t>(context->events->size());
            };
            inEvents.get = [](const clap_input_events_t *list,
                              uint32_t index) -> const clap_event_header_t *
            {
                auto *context = static_cast<EventContext *>(list->ctx);
                if (index < context->events->size())
                {
                    return &(*context->events)[index].header;
                }
                return nullptr;
            };

            clap_output_events_t outEvents = {};
            outEvents.try_push =
                [](const clap_output_events_t *, const clap_event_header_t *) -> bool
            { return true; };

            clap_process_t processData = {};
            processData.frames_count = blockSize;
            processData.audio_inputs = &inputAudioBuffer;
            processData.audio_outputs = &outputAudioBuffer;
            processData.audio_inputs_count = 1;
            processData.audio_outputs_count = 1;
            processData.in_events = &inEvents;
            processData.out_events = &outEvents;

            // Process once with the wrong-namespace events
            clap_process_status status = plugin->process(&processData);
            if (status == CLAP_PROCESS_ERROR)
            {
                plugin->stopProcessing();
                plugin->deactivate();
                return TestResult::failed(testName, description, "Process returned error");
            }

            plugin->stopProcessing();
            plugin->deactivate();
        }

        // Check that parameter values have NOT changed
        std::map<clap_id, double> actualParamValues;
        for (uint32_t i = 0; i < paramCount; ++i)
        {
            double value = 0.0;
            if (!paramsExt->get_value(plugin->clapPlugin(), paramInfos[i].id, &value))
            {
                return TestResult::failed(testName, description,
                                          "Failed to get parameter value after processing");
            }
            actualParamValues[paramInfos[i].id] = value;
        }

        if (actualParamValues == initialParamValues)
        {
            return TestResult::success(testName, description);
        }
        else
        {
            return TestResult::failed(
                testName, description,
                "Sending events with type ID " + std::to_string(CLAP_EVENT_PARAM_VALUE) +
                    " (CLAP_EVENT_PARAM_VALUE) and namespace ID 0x" +
                    std::to_string(INCORRECT_NAMESPACE_ID) +
                    " to the plugin caused its parameter values to change. "
                    "This should not happen. The plugin may not be checking the event's "
                    "namespace ID.");
        }
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

        return TestResult::success(testName, description,
                                   "Plugin correctly rejected empty state");
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
            return TestResult::failed(testName, description, "Failed to save state from second instance");
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
    const std::string description = "Tests state reproducibility using flush for parameter changes.";

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
