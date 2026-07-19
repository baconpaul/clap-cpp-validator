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

#ifndef CLAPVALCPP_SRC_TESTS_PROCESSING_TEST_H
#define CLAPVALCPP_SRC_TESTS_PROCESSING_TEST_H

#include <functional>
#include <memory>
#include "../plugin/process.h"

namespace clap_validator
{

class Plugin;
class Host;

// Handles the boilerplate around testing a plugin's audio processing. Mirrors the Rust validator's
// ProcessingTest: it activates the (still deactivated) plugin, starts processing, calls process()
// a number of times while checking the output for consistency each time, and finally stops and
// deactivates. A preprocess callback runs before each cycle to set up events and randomize buffers.
//
// On any inconsistency it throws std::runtime_error, which the calling test turns into a Failed
// result (matching the Rust Result-to-Failed conversion).
class ProcessingTest
{
  public:
    ProcessingTest(Plugin &plugin, std::shared_ptr<Host> host, AudioBuffers &buffers);

    // Run `numIters` processing cycles. `preprocess` is called before each cycle.
    void run(int numIters, const ProcessConfig &config,
             const std::function<void(ProcessData &)> &preprocess);

    // Run exactly one processing cycle.
    void runOnce(const ProcessConfig &config, const std::function<void(ProcessData &)> &preprocess);

    // Verify out-of-place output: no non-finite or subnormal samples, inputs untouched, and output
    // events in monotonically increasing time order within the buffer. Throws on any violation.
    static void checkOutOfPlaceOutputConsistency(
        ProcessData &processData, const AudioBuffers &buffers,
        const std::vector<std::vector<std::vector<float>>> &originalInputs);

  private:
    Plugin &plugin_;
    std::shared_ptr<Host> host_;
    AudioBuffers &buffers_;
};

} // namespace clap_validator

#endif // CLAPVALCPP_SRC_TESTS_PROCESSING_TEST_H
