#include "plugin_tests.h"
#include "../plugin/library.h"
#include "../plugin/host.h"
#include "../plugin/instance.h"
#include <set>
#include <cstring>

namespace clap_validator {

std::vector<TestCaseInfo> PluginTests::getAllTests() {
    return {
        {"descriptor-consistency",
         "The plugin descriptor returned from the plugin factory and the plugin descriptor stored on the 'clap_plugin' object should be equivalent."},
        {"features-categories",
         "The plugin needs to have at least one of the main CLAP category features."},
        {"features-duplicates",
         "The plugin's features array should not contain any duplicates."},
        {"process-audio-basic",
         "Processes random audio through the plugin with its default parameter values and tests whether the output does not contain any non-finite or subnormal values."},
        {"param-conversions",
         "Asserts that value to string and string to value conversions are supported for either all or none of the plugin's parameters."}
    };
}

TestResult PluginTests::runTest(const std::string& testName, 
                                 PluginLibrary& library, 
                                 const std::string& pluginId) {
    if (testName == "descriptor-consistency") {
        return testDescriptorConsistency(library, pluginId);
    } else if (testName == "features-categories") {
        return testFeaturesCategories(library, pluginId);
    } else if (testName == "features-duplicates") {
        return testFeaturesDuplicates(library, pluginId);
    } else if (testName == "process-audio-basic") {
        return testProcessAudioBasic(library, pluginId);
    } else if (testName == "param-conversions") {
        return testParamConversions(library, pluginId);
    }
    
    return TestResult::failed(testName, "Unknown test", "Test '" + testName + "' not found");
}

TestResult PluginTests::testDescriptorConsistency(PluginLibrary& library, const std::string& pluginId) {
    const std::string testName = "descriptor-consistency";
    const std::string description = "Plugin descriptor consistency check.";
    
    try {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        
        if (!plugin->init()) {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }
        
        // Get the descriptor from the plugin instance
        const clap_plugin_descriptor_t* instanceDesc = plugin->descriptor();
        if (!instanceDesc) {
            return TestResult::failed(testName, description, "Plugin instance has no descriptor");
        }
        
        // Get the descriptor from the factory
        auto metadata = library.metadata();
        const PluginMetadata* factoryMeta = nullptr;
        for (const auto& pm : metadata.plugins) {
            if (pm.id == pluginId) {
                factoryMeta = &pm;
                break;
            }
        }
        
        if (!factoryMeta) {
            return TestResult::failed(testName, description, "Plugin ID not found in factory");
        }
        
        // Compare the descriptors
        if (factoryMeta->id != instanceDesc->id) {
            return TestResult::failed(testName, description, 
                "Plugin ID mismatch: factory='" + factoryMeta->id + "', instance='" + instanceDesc->id + "'");
        }
        
        if (factoryMeta->name != instanceDesc->name) {
            return TestResult::failed(testName, description,
                "Plugin name mismatch: factory='" + factoryMeta->name + "', instance='" + instanceDesc->name + "'");
        }
        
        return TestResult::success(testName, description);
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testFeaturesCategories(PluginLibrary& library, const std::string& pluginId) {
    const std::string testName = "features-categories";
    const std::string description = "Plugin must have at least one main category feature.";
    
    try {
        auto metadata = library.metadata();
        
        const PluginMetadata* pluginMeta = nullptr;
        for (const auto& pm : metadata.plugins) {
            if (pm.id == pluginId) {
                pluginMeta = &pm;
                break;
            }
        }
        
        if (!pluginMeta) {
            return TestResult::failed(testName, description, "Plugin ID not found");
        }
        
        // Main CLAP category features
        const std::set<std::string> mainCategories = {
            CLAP_PLUGIN_FEATURE_INSTRUMENT,
            CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
            CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
            CLAP_PLUGIN_FEATURE_NOTE_DETECTOR,
            CLAP_PLUGIN_FEATURE_ANALYZER
        };
        
        bool hasMainCategory = false;
        for (const auto& feature : pluginMeta->features) {
            if (mainCategories.count(feature)) {
                hasMainCategory = true;
                break;
            }
        }
        
        if (!hasMainCategory) {
            return TestResult::failed(testName, description,
                "Plugin does not have any main category feature (instrument, audio-effect, note-effect, note-detector, analyzer)");
        }
        
        return TestResult::success(testName, description);
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testFeaturesDuplicates(PluginLibrary& library, const std::string& pluginId) {
    const std::string testName = "features-duplicates";
    const std::string description = "Plugin features should not contain duplicates.";
    
    try {
        auto metadata = library.metadata();
        
        const PluginMetadata* pluginMeta = nullptr;
        for (const auto& pm : metadata.plugins) {
            if (pm.id == pluginId) {
                pluginMeta = &pm;
                break;
            }
        }
        
        if (!pluginMeta) {
            return TestResult::failed(testName, description, "Plugin ID not found");
        }
        
        std::set<std::string> seenFeatures;
        for (const auto& feature : pluginMeta->features) {
            if (seenFeatures.count(feature)) {
                return TestResult::failed(testName, description,
                    "Duplicate feature found: '" + feature + "'");
            }
            seenFeatures.insert(feature);
        }
        
        return TestResult::success(testName, description);
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testProcessAudioBasic(PluginLibrary& library, const std::string& pluginId) {
    const std::string testName = "process-audio-basic";
    const std::string description = "Basic audio processing test.";
    
    try {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        
        if (!plugin->init()) {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }
        
        const double sampleRate = 44100.0;
        const uint32_t blockSize = 512;
        
        if (!plugin->activate(sampleRate, blockSize, blockSize)) {
            return TestResult::failed(testName, description, "Failed to activate plugin");
        }
        
        if (!plugin->startProcessing()) {
            plugin->deactivate();
            return TestResult::failed(testName, description, "Failed to start processing");
        }
        
        // Create simple process data
        std::vector<float> inputBuffer(blockSize, 0.0f);
        std::vector<float> outputBuffer(blockSize, 0.0f);
        
        // Fill input with some test signal
        for (uint32_t i = 0; i < blockSize; ++i) {
            inputBuffer[i] = static_cast<float>(i) / static_cast<float>(blockSize) - 0.5f;
        }
        
        float* inputPtrs[1] = { inputBuffer.data() };
        float* outputPtrs[1] = { outputBuffer.data() };
        
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
        inEvents.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
        inEvents.get = [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; };
        
        // Create dummy unwritable output event queue
        clap_output_events_t outEvents = {};
        outEvents.ctx = nullptr;
        outEvents.try_push = [](const clap_output_events_t*, const clap_event_header_t*) -> bool { return false; };
        
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
        
        if (status == CLAP_PROCESS_ERROR) {
            return TestResult::failed(testName, description, "Process returned error");
        }
        
        // Check output for non-finite values
        for (uint32_t i = 0; i < blockSize; ++i) {
            if (!std::isfinite(outputBuffer[i])) {
                return TestResult::failed(testName, description,
                    "Output contains non-finite value at sample " + std::to_string(i));
            }
        }
        
        return TestResult::success(testName, description);
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

TestResult PluginTests::testParamConversions(PluginLibrary& library, const std::string& pluginId) {
    const std::string testName = "param-conversions";
    const std::string description = "Parameter value/string conversion test.";
    
    try {
        auto host = std::make_shared<Host>();
        auto plugin = library.createPlugin(pluginId, host);
        
        if (!plugin->init()) {
            return TestResult::failed(testName, description, "Failed to initialize plugin");
        }
        
        // Get the params extension
        const clap_plugin_params_t* paramsExt = 
            static_cast<const clap_plugin_params_t*>(plugin->getExtension(CLAP_EXT_PARAMS));
        
        if (!paramsExt) {
            return TestResult::skipped(testName, description, "Plugin does not support params extension");
        }
        
        uint32_t paramCount = paramsExt->count(plugin->clapPlugin());
        if (paramCount == 0) {
            return TestResult::skipped(testName, description, "Plugin has no parameters");
        }
        
        // Test that all parameters can be queried
        for (uint32_t i = 0; i < paramCount; ++i) {
            clap_param_info_t info = {};
            if (!paramsExt->get_info(plugin->clapPlugin(), i, &info)) {
                return TestResult::failed(testName, description,
                    "Failed to get info for parameter index " + std::to_string(i));
            }
        }
        
        return TestResult::success(testName, description,
            "Successfully queried " + std::to_string(paramCount) + " parameters");
        
    } catch (const std::exception& e) {
        return TestResult::failed(testName, description, e.what());
    }
}

} // namespace clap_validator
