# clap-cpp-validator — Validation Coverage Roadmap

**CLAP target:** 1.2.10 · **Validator baseline:** September 2023 · **Analysis:** July 2026

## Context

`clap-cpp-validator` is a C++ port of the (archived) Rust `clap-validator`. The Rust project's last
substantive update was **September 2023**. Since then CLAP has moved a long way: on **2023‑12‑20** a
large batch of extensions graduated from `draft/` to stable (`preset-load`, `remote-controls`,
`param-indication`, `state-context`, `audio-ports-activation`, `configurable-audio-ports`,
`context-menu`, `surround`, `track-info`), and many more were added through the 1.2.x line up to
1.2.10. **Every one of those stable extensions postdates the validator's baseline**, so the tool has
no knowledge of them.

Today the header tree ships **27 stable + 18 draft extensions** and **4 factories**. The validator
actively exercises just five plugin extensions, implements three host extensions, and uses two
factories (see [Current coverage](#current-coverage)). This document proposes where to expand,
organized into three buckets:

1. [Untested / unvalidated extensions & factories](#category-1--untested--unvalidated-extensions--factories)
2. [Changed documentation / constraints we can now validate](#category-2--changed-documentation--constraints-we-can-now-validate)
3. [Additional cross-cutting constraints & invariants](#category-3--additional-cross-cutting-constraints--invariants)

**Legend:** effort **S**/**M**/**L** (small/medium/large) · ★ = high value / recommended first.

## Current coverage

- **Plugin extensions exercised:** `audio-ports`, `note-ports`, `params`, `state`, `preset-load`.
- **Host extensions implemented:** `thread-check`, `params` (rescan/clear/request_flush → flush),
  `state` (mark_dirty).
- **Factories used:** `plugin-factory` (descriptor + create), `preset-discovery`.

Everything else in the header tree is currently unobserved. A full inventory is in the
[Appendix](#appendix--extension--factory-inventory).

---

## Category 1 — Untested / unvalidated extensions & factories

### 1a. Plugin extensions to query & validate

The plugin provides these; the host reads them and asserts invariants. Cheap, safe, read-mostly.

| Extension | Effort | What a check verifies |
|---|---|---|
| `latency` ★ ✅ | S | `get()` readable while active and stable across reads. **Implemented.** |
| `tail` ✅ | S | `get()` (while active) readable and stable. **Implemented.** |
| `render` ✅ | S | `set(REALTIME)` accepted; a hard-realtime plugin rejects `OFFLINE`. **Implemented.** |
| `voice-info` ✅ | S | `1 ≤ voice_count ≤ voice_capacity` while active. **Implemented.** |
| `note-name` ✅ | S | Enumerate; validate key (`-1`/`0..127`), channel, and port ranges. **Implemented.** |
| `audio-ports-config` ★ | M | Enumerate configs (unique ids); `select` each; confirm `audio-ports` then reflects the selected layout. |
| `remote-controls` ★ | M | Enumerate pages; every `param_id` exists in `params`; unique page ids; correct slot count. |
| `state-context` ★ | M | Reproducibility per `FOR_PRESET`/`FOR_PROJECT`/`FOR_DUPLICATE`; agrees with plain `state`. |
| `audio-ports-activation` / `configurable-audio-ports` / `extensible-audio-ports` | M | Exercise (de)activation & reconfiguration APIs, then re-check `audio-ports`. |
| `param-indication` | S | Call `set_mapping`/`set_automation`; smoke-test for no crash and correct thread. |
| `context-menu` ✅ | M | `populate` a target; validate the returned entry structure. **Implemented** (`context-menu` check). |
| `gui` | M | `is_api_supported`/`get_preferred_api`; create → get_size → destroy **without showing**; `can_resize`/`adjust_size` sanity. Platform-sensitive. |
| `surround` / `ambisonic` | M | Channel-mask / channel-map queries. Niche. |

### 1b. Host extensions to implement

The plugin calls these on the host. The validator's host currently implements only `thread-check`,
`params`, and `state`, so plugins asking for anything else receive `null` — which both
under-exercises them and can hide misbehavior.

| Host extension | Effort | Why |
|---|---|---|
| `log` ★★ ✅ | S | Surface `CLAP_LOG_WARNING/ERROR/FATAL` and especially `*_MISBEHAVING` messages as findings — plugins report their own conformance problems here. **Implemented:** the host prints `WARNING`+ and treats `*_MISBEHAVING` as findings. |
| `preset-load` (host side) ★ ✅ | S | Implement `on_error`/`loaded`. **Implemented:** `on_error` now surfaces a failed preset load into the `preset-discovery-load` check. |
| `timer-support` / `posix-fd-support` | S | Plugins commonly register these in `init()`; implementing them avoids null-host-ext paths. *(Not yet done.)* |
| `thread-pool` | M | Implement `request_exec` so plugins that fan out work actually run and are validated. *(Not yet done.)* |
| `changed()` callbacks for `latency`, `tail`, `note-name`, `voice-info`, `audio-ports` ✅ | S each | **Implemented** as faithful, thread-asserting host callbacks. (`remote-controls` host still to do.) |

### 1c. Untested factories

| Factory | Effort | What a check verifies |
|---|---|---|
| `plugin-invalidation` | S | Enumerate invalidation sources; validate the declared paths. |
| `plugin-state-converter` ★ | M | Enumerate converters; validate src/dst descriptors; optionally convert a saved state and load it into the target plugin. |

---

## Category 2 — Changed documentation / constraints we can now validate

Spec clarifications from the 1.2.x ChangeLog that translate directly into checks:

- **`latency` requirements changed (1.2.2)** — latency must be queried after activation, must be
  constant while active, and any change requires deactivate + `request_restart`. New concrete check.
- **`params` flag clarifications** ✅ — `CLAP_PARAM_IS_BYPASS` clarified (1.2.10); the flag set now
  includes **`CLAP_PARAM_IS_ENUM`** and **`CLAP_PARAM_IS_PERIODIC`**. **Implemented:** param-info
  validation now enforces `ENUM ⇒ STEPPED`. (The `ENUM` "no blank value_to_text over the range"
  rule is still to do.)
- **`thread-check` realtime docs expanded (1.2.2 / 1.2.4)** — extend which host callbacks we assert
  are main-thread vs audio-thread.
- **`events.h` sysex lifetime clarified (1.2.3)** — relevant when the note fuzzer emits sysex.
- **`preset-discovery` / `preset-load` graduated to `/2` stable + compat ids** — the factory compat
  fallback is handled; confirm the extension compat id path too.
- **`host.request_callback` clarified (1.2.3)** — already honored by `handleCallbacksOnce`; worth
  codifying as an explicit tested behavior.

---

## Category 3 — Additional cross-cutting constraints & invariants

Behavioral conformance not tied to the mere presence of one extension — the highest-signal checks:

- **Process return-value semantics** ★ — the `process()` return is currently ignored except for
  `ERROR`. Validate it's a legal value, and that an effect fed silence past its `tail` eventually
  returns `SLEEP`.
- **Parameter defaults** ★ ✅ (S) — **Implemented** (`param-defaults`): a freshly created plugin's
  `get_value` for each parameter must equal its declared `default_value`.
- **Plugin lifecycle state machine** ★ (M) — `activate` twice should fail; `process` /
  `start_processing` before `activate` should error; `deactivate` while processing; reactivate at
  different sample rates and `min ≠ max` block sizes (including 1-sample and large blocks), varying
  `frames_count`.
- **`get_extension` contract** (S) — returns the *same* pointer on repeated calls, `null` for
  unknown ids, callable after `init`.
- **Note lifecycle** (M) — a note-output plugin should emit `CLAP_EVENT_NOTE_END` for note-ids it
  finishes, referencing note-ids it actually started.
- **Param range clamping / robustness** (S) — out-of-range parameter events must not yield
  out-of-range `get_value` and must not crash.
- **NaN/Inf/denormal *input* resilience** (S) — feed pathological input audio; the plugin must not
  emit NaN/Inf (complements the existing output-finite check).
- **DSP determinism** (M) — identical input + parameters on two fresh instances produce identical
  output. Opt-in, since some plugins are legitimately stochastic.
- **f64 & in-place processing** (M) — only out-of-place f32 is tested today; exercise `data64` when
  advertised and the in-place-pair audio-port info that is currently ignored.
- **Cookie / flush stability** ✅ (S) — **Implemented** (`param-info-stable`): `get_info` ids,
  cookies, ranges, and flags are stable across repeated queries.

---

## Suggested first tranche

Highest bang-for-buck to start:

1. **Host `log`** — surfaces plugins' own self-reported conformance problems (1b, S).
2. **Trivial read-checks** — `latency`, `tail`, `voice-info`, `note-name`, `render` (1a, S).
3. **Cheap Category 3 wins** — parameter defaults, `get_extension` contract, lifecycle state machine.
4. **`state-context`** — extends the existing, well-tested state machinery (1a, M).

Each is small and independent, so they can land as separate reviewed changes.

---

## Appendix — extension & factory inventory

**Currently exercised** (8 extensions + 2 factories):
`audio-ports`, `note-ports`, `params`, `state`, `preset-load` (plugin) · `thread-check`, `params`,
`state` (host) · `plugin-factory`, `preset-discovery` (factories).

**Stable extensions not yet exercised (22):**
`ambisonic`, `audio-ports-activation`, `audio-ports-config`, `configurable-audio-ports`,
`context-menu`, `event-registry`, `gui`, `latency`, `log`, `note-name`, `param-indication`,
`posix-fd-support`, `remote-controls`, `render`, `state-context`, `surround`, `tail`, `thread-pool`,
`timer-support`, `track-info`, `voice-info`. *(All of the 2023‑12‑20 graduation batch is here.)*

**Draft extensions not yet exercised (18):**
`background-activation`, `background-progress`, `background-state-context`, `extensible-audio-ports`,
`flush-events`, `gain-adjustment-metering`, `mini-curve-display`, `octave-number`, `param-hovered`,
`params-origin`, `project-location`, `resource-directory`, `scratch-memory`, `transport-control`,
`triggers`, `tuning`, `undo`, `webview`.

**Factories not yet exercised (2):**
`plugin-invalidation`, `plugin-state-converter`.
