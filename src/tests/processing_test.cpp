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
#include "processing_test.h"
#include "../plugin/instance.h"
#include "../plugin/host.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace clap_validator
{

ProcessingTest::ProcessingTest(Plugin &plugin, std::shared_ptr<Host> host, AudioBuffers &buffers)
    : plugin_(plugin), host_(std::move(host)), buffers_(buffers)
{
}

void ProcessingTest::run(int numIters, const ProcessConfig &config,
                         const std::function<void(ProcessData &)> &preprocess)
{
    host_->clearRequestedRestart();

    uint32_t bufferSize = buffers_.numSamples();
    ProcessData processData(buffers_, config);

    // If the plugin requests a restart mid-processing we stop, deactivate, reactivate, and start
    // again, so we track completed iterations manually rather than with a for loop.
    int itersDone = 0;
    while (itersDone < numIters)
    {
        if (!plugin_.activate(config.sampleRate, 1, bufferSize))
        {
            throw std::runtime_error("Failed to activate the plugin for processing.");
        }

        {
            AudioThreadGuard guard(host_);

            if (!plugin_.startProcessing())
            {
                plugin_.deactivate();
                throw std::runtime_error("Failed to start processing.");
            }

            while (itersDone < numIters)
            {
                itersDone++;

                preprocess(processData);

                // Snapshot the inputs so we can verify the plugin doesn't modify them.
                auto originalInputs = buffers_.inputsCopy();

                clap_process_t clapProcess = processData.clapProcess();
                clap_process_status status = plugin_.process(&clapProcess);
                if (status == CLAP_PROCESS_ERROR)
                {
                    throw std::runtime_error(
                        "The plugin returned an error during audio processing.");
                }

                checkOutOfPlaceOutputConsistency(processData, buffers_, originalInputs);

                processData.clearEvents();
                processData.advanceTransport(bufferSize);

                if (host_->hasRequestedRestart())
                {
                    host_->clearRequestedRestart();
                    break;
                }
            }

            plugin_.stopProcessing();
        }

        plugin_.deactivate();
    }

    // Handle callbacks the plugin may have made while it was active.
    host_->handleCallbacksOnce();
}

void ProcessingTest::runOnce(const ProcessConfig &config,
                             const std::function<void(ProcessData &)> &preprocess)
{
    run(1, config, preprocess);
}

void ProcessingTest::checkOutOfPlaceOutputConsistency(
    ProcessData &processData, const AudioBuffers &buffers,
    const std::vector<std::vector<std::vector<float>>> &originalInputs)
{
    uint32_t numSamples = buffers.numSamples();

    if (buffers.inputs() != originalInputs)
    {
        throw std::runtime_error(
            "The plugin has overwritten the input buffers during out-of-place processing.");
    }

    const auto &outputs = buffers.outputs();
    for (size_t port = 0; port < outputs.size(); ++port)
    {
        for (size_t ch = 0; ch < outputs[port].size(); ++ch)
        {
            for (size_t s = 0; s < outputs[port][ch].size(); ++s)
            {
                float value = outputs[port][ch][s];
                const std::string where = "output port " + std::to_string(port) + ", channel " +
                                          std::to_string(ch) + ", sample " + std::to_string(s);
                if (!std::isfinite(value))
                {
                    throw std::runtime_error("The sample written to " + where + " is non-finite.");
                }
                if (std::fpclassify(value) == FP_SUBNORMAL)
                {
                    throw std::runtime_error("The sample written to " + where + " is subnormal.");
                }
            }
        }
    }

    // Any events the plugin output should be in monotonically increasing time order.
    uint32_t lastEventTime = 0;
    for (const auto &event : processData.outputEvents().events())
    {
        uint32_t eventTime = event.header()->time;
        if (eventTime < lastEventTime)
        {
            throw std::runtime_error("The plugin output an event for sample " +
                                     std::to_string(eventTime) +
                                     " after it had previously output an event for sample " +
                                     std::to_string(lastEventTime) + ".");
        }
        lastEventTime = eventTime;
    }
    if (lastEventTime >= numSamples)
    {
        throw std::runtime_error("The plugin output an event for sample " +
                                 std::to_string(lastEventTime) + " but the buffer only contains " +
                                 std::to_string(numSamples) + " samples.");
    }
}

} // namespace clap_validator
