# clap-cpp-validator design

This document explains how the C++ CLAP validator is put together: how a plugin is loaded and
hosted, how the conformance checks are structured and run, and the shared scaffolding the checks
build on. It is meant for someone extending the validator or porting more checks.

## What this is

`clap-cpp-validator` is a command-line tool that loads a real `.clap` binary and runs a battery of
conformance **checks** against it. It is a C++ re-implementation of the (archived) Rust
[`clap-validator`](https://github.com/free-audio/clap-validator); the Rust source is kept under
`orig-validator/` purely as a reference. Throughout the code, "test" means "a validator check run
against an external plugin" — there is no unit-test framework, and the tool does not ship a test
plugin. Verification is done by pointing the built binary at real plugins.

## Building and running

The project is a single CMake executable target (`clap-validator`), C++20, depending only on the
CLAP C headers (`libs/clap`, a submodule).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/clap-validator validate /path/to/plugin.clap
./build/clap-validator list tests          # enumerate every check id + description
```

Useful flags: `--test <regex>` (filter checks), `--plugin-id <id>` (one plugin in a multi-plugin
bundle), `--json`, `--only-failed`, `--in-process`. `USE_ASAN=ON` builds with ASan/UBSan.

## High-level architecture

```
main.cpp ─▶ commands/validate.cpp (the runner loop)
                │  loads the library, iterates checks, prints/JSON-formats results
                ▼
        plugin/  ── the host side: load the binary and host a plugin instance
          library.{h,cpp}   PluginLibrary: dlopen, entry point, factory, metadata
          host.{h,cpp}      Host: clap_host callbacks + thread-safety checks
          instance.{h,cpp}  Plugin: init/activate/process/state lifecycle wrapper
          process.{h,cpp}   EventQueue / AudioBuffers / ProcessData (process scaffolding)
          ext.{h,cpp}       audio-ports / note-ports / params query wrappers
                ▲
        tests/  ── the check side
          test_case.{h,cpp}         TestResult / TestStatusCode
          plugin_tests.{h,cpp}      per-plugin-instance checks
          plugin_library_tests.*    per-library checks
          rng.{h,cpp}               deterministic PRNG, NoteGenerator, ParamFuzzer
          processing_test.{h,cpp}   ProcessingTest harness + output-consistency checks
```

## The check model

Checks come in two families:

- **Per-library checks** (`PluginLibraryTests`) take a `.clap` path and inspect the library as a
  whole — scan time, factory queries, preset discovery.
- **Per-plugin checks** (`PluginTests`) take a loaded `PluginLibrary` plus a plugin id and exercise
  a single plugin instance — descriptor, processing, params, state.

There is no base class or registry object. Each family is a class of `static` methods, and a check
is a hand-maintained triple:

1. A `{ "kebab-id", "description" }` entry returned from `getAllTests()`.
2. A dispatch branch in `runTest()` (`if (testName == "kebab-id") return testXxx(...)`).
3. The implementation `static TestResult testXxx(...)`.

Every implementation follows the same shape and wraps its body in
`try { ... } catch (const std::exception &e) { return TestResult::failed(name, desc, e.what()); }`.
Throwing `std::runtime_error` is the C++ stand-in for Rust returning `Err`, and it becomes a
`Failed` result. Preconditions that aren't met (a missing extension, no parameters) return
`TestResult::skipped(...)`.

`TestResult` (`tests/test_case.h`) carries a name, description, `TestStatusCode`
(`Success`/`Crashed`/`Failed`/`Skipped`/`Warning`), and optional details.

**Adding a check:** add the registry entry, the dispatch branch, and the implementation method
(declared in the corresponding `.h`). Match the kebab-case id to the Rust validator's id string —
that string is the stable identity across the two implementations.

## The host / plugin layer

- **`PluginLibrary`** loads the `.clap` (a shared library / bundle), resolves the `clap_entry`
  point, exposes the plugin factory, and reads per-plugin metadata (id, name, features, …).
- **`Host`** implements `clap_host` and a broad set of host extensions so plugins reach real
  callbacks rather than null: `thread-check`, `params`, `state`, `log`, `audio-ports`, `note-ports`,
  `latency`, `tail`, `note-name`, `voice-info`, and `preset-load`. Most are faithful no-ops that
  assert the calling thread; the notable ones with findings are `log` (below) and `preset-load`
  (`on_error` surfaces a failed preset load into the `preset-discovery-load` check). The `log` sink
  prints `WARNING`+ messages (subject to `--show-plugin-stdout`) and turns `PLUGIN_MISBEHAVING`
  messages into findings via the callback-error path; `HOST_MISBEHAVING` is printed but not treated
  as a plugin failure, since it indicts the host rather than the plugin.
  It records the main-thread id and, via `AudioThreadGuard` (an RAII marker), the audio-thread id,
  so host callbacks made from the wrong thread are recorded as callback errors. `handleCallbacksOnce()`
  drains a pending `request_callback` by invoking the plugin's `on_main_thread`, and a pending
  `clap_host_params::request_flush` by calling `clap_plugin_params::flush()` (with empty events)
  while the plugin is inactive — honoring the flush request the way a real host would, which
  matters for clap-first plugins that defer initializing their parameter model until the first
  flush.
- **`Plugin`** wraps a `clap_plugin` instance and its lifecycle (`init` → `activate` →
  `startProcessing` → `process` → `stopProcessing` → `deactivate`), plus `getExtension`,
  `descriptor`, and `onMainThread`.

## Test scaffolding

These pieces (ported from the Rust validator's `rng.rs`, `instance/process.rs`, `processing.rs`,
and `ext/*.rs`) are shared by the processing, parameter, and state checks.

### Deterministic RNG — `tests/rng.h`

`Prng` wraps a **fixed-seed `std::mt19937`**. Engines in `<random>` are specified exactly by the
standard and produce identical raw output on every platform, so a fixed seed makes fuzz/note/param
inputs reproducible — and, importantly, reproducible *across* the Windows/macOS/Linux CI matrix. We
deliberately do **not** use `std::uniform_int_distribution` / `std::uniform_real_distribution`
(their algorithms are unspecified and diverge between standard libraries); `Prng` provides its own
`nextInt`/`nextIntInclusive`/`nextFloat`/`nextDouble` helpers instead. The exact value sequence is
not identical to the Rust validator's PCG32 — that was a deliberate scope decision — but it is
deterministic everywhere this tool runs.

- **`NoteGenerator`** produces random CLAP note / note-expression / MIDI events consistent with a
  plugin's `NotePortConfig` (no note-offs before note-ons, no expressions for dead notes, etc.), or
  intentionally inconsistent ones via `withInconsistentEvents()`.
- **`ParamFuzzer`** emits `CLAP_EVENT_PARAM_VALUE` events randomizing every non-readonly,
  non-hidden parameter (optionally with null cookies).

### Process scaffolding — `plugin/process.h`

- **`Event`** — a union over the concrete CLAP event structs (they share a `clap_event_header`).
- **`EventList`** — an event queue that exposes both `clap_input_events` and `clap_output_events`
  vtables over the same storage; non-movable so the vtable `ctx` pointers stay stable.
- **`AudioBuffers`** — owns per-port/channel out-of-place `float` buffers, builds the
  `clap_audio_buffer` pointer structures, and can `randomize()` to white noise (subnormals snapped
  to zero).
- **`ProcessData`** — assembles the `clap_process` struct (buffers, event queues, transport) and
  advances the transport between cycles.

### Processing harness — `tests/processing_test.h`

`ProcessingTest` runs the standard cycle for a still-deactivated plugin: activate → start → call
`process()` N times (invoking a `preprocess` callback before each to set events / randomize
buffers) → stop → deactivate, honoring a mid-run `request_restart`.
`checkOutOfPlaceOutputConsistency` asserts the output has no non-finite or subnormal samples, the
inputs were not modified, and output events are in monotonically increasing time order within the
buffer.

### Extension query wrappers — `plugin/ext.h`

`AudioPortConfig::query`, `NotePortConfig::query`, and `ParamsExt` read a plugin's `audio-ports`,
`note-ports`, and `params` extensions into plain structs, running the same consistency checks the
Rust validator does (channel-count/port-type agreement, single preferred note dialect, parameter
range/flag sanity). They return `std::nullopt` when the extension is absent and throw on an
inconsistency.

> Two copy-paste bugs in the Rust reference are fixed here and flagged in comments: note-expression
> events are correctly typed `CLAP_EVENT_NOTE_EXPRESSION` (the Rust code used `NOTE_CHOKE`), and
> output note ports are queried with `is_input = false`.

## Preset discovery

`plugin/preset_discovery.{h,cpp}` implements the host side of the CLAP preset-discovery factory
(`clap.preset-discovery-factory/2`, with the `draft-2` compat id as a fallback):

- **`PresetDiscoveryFactory`** wraps the factory: `metadata()` lists every provider (rejecting null
  descriptors and duplicate ids) and `createProvider()` instantiates one after a CLAP-version check.
- **`Indexer`** is the host callback object passed to `provider->init()`. The plugin declares its
  file types and locations into it; the indexer validates them (file extensions can't start with a
  `.`, `FILE` locations must be absolute paths, `PLUGIN` locations must have a null path) and records
  the first error.
- **`Provider`** creates + initializes the provider and reads back the declared data.
  `crawlLocation()` walks a declared location — a single file is queried directly, a directory is
  recursively walked and filtered by the declared extensions, and internal (`PLUGIN`) locations are
  queried with a null path — calling `get_metadata()` for each candidate.
- **`MetadataReceiver`** is the per-`get_metadata()` callback object. It runs the begin-preset state
  machine (single vs. container files, load keys, mandatory names for container presets), collects
  each preset's plugin ids, and records errors (including missing `begin_preset()` calls and
  cross-thread callbacks).

The checks: `preset-discovery-crawl` indexes every location of every provider; `-load` additionally
loads each discovered preset (grouped by CLAP plugin id, via the `preset-load` extension) and
processes a buffer after each; `-descriptor-consistency` compares each provider's own descriptor to
the factory's.

## Execution model

By default each check runs in its own child process, so a plugin that crashes takes down only that
one check (reported as `Crashed`) instead of the whole validator. The runner (`commands/validate.cpp`)
enumerates the plugins in the parent, then for each check spawns the validator binary again with the
hidden `run-single-test` subcommand:

```
run-single-test --output-file <file> <library|plugin> <path> [<plugin-id>] <test-name>
```

The child runs the single check in-process and writes its result (status, name, optional details) to
the given file using a simple self-delimiting text format — no JSON escaping needed for multi-line
details. The parent waits on the child: a clean exit reads the result back; a non-zero exit or a
terminating signal becomes a `Crashed` result naming the signal. `--in-process` forces everything
into a single process (the old behavior, where a crash is fatal). Windows always runs in-process for
now — the subprocess path is POSIX-only (`fork`/`execvp`/`waitpid`).

## The checks

Run `clap-validator list tests` for the authoritative list. As of this writing:

### Per-library checks

| id | what it verifies |
|---|---|
| `scan-time` | library load + metadata read completes under 100 ms (else Warning) |
| `scan-rtld-now` | on Unix, the library loads under `RTLD_LOCAL|RTLD_NOW` (eager symbol resolution) |
| `query-factory-nonexistent` | querying a bogus factory id returns null |
| `create-id-with-trailing-garbage` | creating `<valid-id> + garbage` returns null |
| `preset-discovery-crawl` | every declared preset location can be indexed |
| `preset-discovery-descriptor-consistency` | provider descriptors match the factory's |
| `preset-discovery-load` | every discovered preset can be loaded and processed |

### Per-plugin checks

| id | what it verifies |
|---|---|
| `descriptor-consistency` | factory descriptor equals the instance's `clap_plugin` descriptor |
| `features-categories` | plugin has ≥1 main category feature |
| `features-duplicates` | features array has no duplicates |
| `process-audio-out-of-place-basic` | random audio at default params processes consistently |
| `process-note-out-of-place-basic` | random consistent note/MIDI events process consistently |
| `process-note-inconsistent` | intentionally inconsistent note events still process consistently |
| `param-conversions` | value↔text conversions are all-or-none and roundtrip |
| `param-fuzz-basic` | random parameter values + audio/notes produce no NaN/Inf and no crash |
| `param-set-wrong-namespace` | param events with a wrong namespace id are ignored |
| `state-invalid` | loading empty state returns false |
| `state-reproducibility-basic` | randomize → save → recreate → load → re-save is reproducible |
| `state-reproducibility-null-cookies` | same, with null param-event cookies |
| `state-reproducibility-flush` | same, using `params.flush()` for the second instance |
| `state-buffered-streams` | reproducibility with small chunked state reads/writes |
| `context-menu` | the plugin's global and per-parameter `context-menu` items are well-formed (non-null labels/titles, balanced submenus, known kinds) |
| `latency` | `latency.get()` is readable while active and stable across reads |
| `tail` | `tail.get()` is readable (while active) and stable |
| `voice-info` | reports `1 ≤ voice_count ≤ voice_capacity` while active |
| `note-name` | every declared note name queries successfully with valid key/channel/port ranges |
| `render` | realtime mode is accepted; a hard-realtime plugin rejects offline mode |
| `param-defaults` | a freshly created plugin's parameter values equal their declared `default_value` |
| `param-info-stable` | parameter info (ids, cookies, ranges, flags) is identical across repeated queries |
| `audio-ports-config` | each advertised `audio-ports-config` selects, and the audio ports then match it |

> `context-menu` and the five extension read-checks below it are not part of the Rust validator;
> they are the new-extension checks from [validation-roadmap.md](validation-roadmap.md).
