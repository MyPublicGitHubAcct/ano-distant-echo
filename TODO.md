# Guitar Effects Project TODO

## 1. Project Setup

- [x] Initialize a git repository at the project root
- [x] Define the top-level directory structure:
  ```
  /
  ├── libs/
  │   ├── effects/    # shared C++ DSP library (no JUCE or VCV Rack headers allowed)
  │   └── juce/       # JUCE git submodule
  ├── juce-plugin/    # JUCE AudioProcessor + UI
  ├── vcv-rack/       # VCV Rack module
  ├── python/         # Python prototypes and golden-output validation scripts
  ├── tests/          # Catch2 unit tests + WAV validation tooling
  └── docs/           # Markdown documentation and DSP plots
  ```
- [x] Install core build tools: `brew install cmake ninja`
- [x] Write a root `CMakeLists.txt` that builds all C++ targets
- [x] Add a `.gitignore` covering build artifacts, Python venvs, IDE files, and JUCE/VCV SDK paths
- [x] Document toolchain requirements in a `README.md` (compiler versions, SDK locations, Python version)

## 2. Python Prototyping Environment

- [x] Install `uv` if not already present: `brew install uv`
- [x] Install `ruff` if not already present: `brew install ruff`
- [x] Initialize the project with `uv init python/` and set the Python version in `python/.python-version`
- [x] Add DSP dependencies: `uv add numpy scipy matplotlib`
- [x] Add audio I/O dependencies: `uv add soundfile` (or `pedalboard`) for loading/saving WAV files
- [x] Run scripts via `uv run python/...` so the lockfile-pinned environment is always used
- [x] Write a shared `python/utils.py` with helpers: sample rate constants, block processing, frequency-domain analysis
- [x] For each effect, write a self-contained Python module that:
  - Implements the algorithm using numpy/scipy
  - Accepts a WAV input and writes a processed WAV output
  - Plots frequency response or time-domain results for validation
- [x] Write comparison scripts that diff Python output against C++ output to catch drift

## 3. Decide on Effects to Implement

- [x] List all target effects (e.g., overdrive/distortion, delay, reverb, chorus, compressor, EQ, wah, tremolo)
- [x] For each effect, identify the reference algorithm or circuit to emulate
- [x] Prioritize the list and prototype in Python before moving to C++

**Implemented (Phase 1):**

- **Overdrive** — five `DistortionType` waveshapers (HardClip, SoftClip, Foldback, Asymmetric, Bitcrush), three `ClipShape` pre/de-emphasis modes, 4× oversampled via polyphase FIR, mid peaking EQ at 800 Hz, presence high shelf at 4 kHz, pick-sensitivity envelope follower; see `docs/Overdrive.md`
- **Delay** — circular buffer delay with single-pole LP (4 kHz) in feedback path emulating tape and BBD character; extended features planned in sections 14 and 17; see `docs/Delay.md`

**Planned (Phase 2 — extended Overdrive/Delay):** sections 14–19 cover fractional interpolation, tape saturation, wow/flutter, diffusion, self-oscillation, advanced distortion types (bias, sag, gated fuzz, octave fuzz), and creative delay modes (reverse, multi-tap, pitch-shifted feedback, Karplus-Strong).

**Planned (Phase 3 — new standalone effects):** sections 23–25 add Reverb (spring and plate), Tremolo (optical and harmonic), and Compressor (standalone RMS compressor/limiter); all follow the same Python-first, golden-WAV validation workflow and share `libs/effects/`.

## 4. Shared C++ Effects Library (`effects/`)

- [x] Design a common C++ interface for all effects (e.g., a base `Effect` class with `prepare(sampleRate, blockSize)` and `process(buffer)`)
- [x] Implement each effect in C++ after validating the Python prototype
- [x] Write Catch2 unit tests for each effect (see section 8 for test structure details)
- [x] Add CMake targets so the library builds as a static lib linkable by both JUCE and VCV Rack
- [x] Validate C++ output against Python output using the comparison scripts from step 2
- [x] Handle parameter smoothing to avoid zipper noise on control changes
- [x] Keep the library free of JUCE or VCV Rack headers — pure standard C++

## 5. JUCE Plugin (`juce-plugin/`)

- [x] Add JUCE as a git submodule or use CPM/FetchContent in CMake
- [x] Set up `juce-plugin/CMakeLists.txt` using `juce_add_plugin`
- [x] Configure target plugin formats: VST3, AU (macOS), Standalone
- [x] Create an `AudioProcessor` subclass that instantiates effects from the shared library
- [x] Expose effect parameters as `AudioProcessorValueTreeState` parameters
- [x] Build a plugin editor (GUI) with knobs/sliders mapped to each parameter
- [ ] Test the standalone build with audio input (microphone or loopback)
- [ ] Test the VST3 in a DAW (Reaper, Logic, Ableton) with a guitar DI track
- [ ] Validate that processed output matches Python reference output at the sample level (or within acceptable floating-point tolerance)

## 6. VCV Rack Module (`vcv-rack/`)

- [x] Install the VCV Rack SDK and Rack source (for building plugins)
- [x] Scaffold a new plugin using `helper.py init` from the Rack SDK
- [x] Link the shared effects library into the Rack plugin CMake target
- [x] For each effect, create a `Module` subclass:
  - Map CV inputs to effect parameters
  - Map audio input/output ports to the effect's `process()` call
  - Handle VCV Rack's sample rate and block size (typically 1 sample at a time)
- [x] Design panel SVGs for each module (use Inkscape or Affinity Designer)
- [x] Register all modules in `plugin.cpp`
- [ ] Test each module in VCV Rack with audio signals
- [x] Package the plugin for distribution (zip with the `.so`/`.dylib` and `plugin.json`)

## 7. Build System

- [x] Confirm the root CMake build compiles all three targets: `effects`, `juce-plugin`, `vcv-rack`
- [x] Add a `tests` CMake target that runs the C++ unit tests via CTest (`ctest --output-on-failure`)
- [x] Add a Python `Makefile` or `justfile` target that runs all Python validation scripts
- [x] Verify builds on macOS (and optionally Linux/Windows) from a clean checkout
- [x] Add a `make test` phony target to the `Makefile` that builds `effects_tests` via CMake and then runs `ctest --test-dir build --output-on-failure`; currently the two validation pipelines (Catch2 unit tests via `ctest` and golden WAV comparison via `make compare`) have no shared entry point, requiring separate invocations; a combined `make test` creates a single command for CI and post-change verification.

## 8. C++ Testing with Catch2

- [x] Install Catch2: `brew install catch2`
- [x] In `tests/CMakeLists.txt`, find and link Catch2 via:

  ```cmake
  find_package(Catch2 3 REQUIRED)
  target_link_libraries(effects_tests PRIVATE effects Catch2::Catch2WithMain)
  include(Catch)
  catch_discover_tests(effects_tests)
  ```

- [x] Create `tests/CMakeLists.txt` with a `catch_discover_tests`-based test executable linked against the `effects` static lib and `Catch2::Catch2WithMain`
- [x] Organize test files one-per-effect (e.g., `tests/test_overdrive.cpp`, `tests/test_delay.cpp`)
- [x] Write tests covering:
  - DC blocking: silence in → silence out
  - Unity/bypass: dry signal passes through when effect is at zero depth
  - Known I/O pairs derived from the Python prototype (hard-coded sample vectors)
  - Parameter bounds: no NaN/Inf output at extreme knob values
  - `prepare()` / `process()` at multiple sample rates (44100, 48000, 96000)
- [x] Use Catch2 `GENERATE` to parameterize sample-rate and block-size combinations
- [x] Use `Approx` (or `WithinAbs`) matchers for floating-point sample comparisons
- [x] Register the test executable with CTest using `catch_discover_tests(effects_tests)`

## 9. Testing and Validation Strategy

- [x] Define acceptable tolerance between Python and C++ output (5e-4; float32 biquad vs float64 sosfilt sets the floor — actual algorithm bugs cause errors >> 1e-3)
- [x] Create a set of reference WAV files (dry guitar samples at various dynamics) — synthetic sines in `tests/golden/input/`
- [x] Run each effect on the reference files in Python and save "golden" output WAVs — `make golden`
- [x] Run the same files through the C++ library and diff against golden outputs — `make compare` (27/27 pass)
- [ ] Run the JUCE standalone with reference files and compare (not automatable via CLI without a custom offline rendering mode)
- [ ] Manually A/B test the VCV Rack module and JUCE plugin against the Python output

## 10. Documentation

- [x] Add `CLAUDE.md` with architecture overview, build commands, and development conventions
- [x] Write a `README.md` describing the project, directory layout, and build instructions
- [x] Document each effect: the algorithm used, any key parameters, and known limitations
- [x] Add inline comments to C++ code only where the DSP math is non-obvious
- [x] Write `docs/Design.md` covering the Python-first prototyping workflow, golden-WAV validation pipeline, Catch2 test strategy, and guidelines for adding new effects

## 11. Plugin Stability

- [x] Add a `reset()` virtual method to `Effect` base class (`effects/Effect.h`) that zeroes all filter history and buffer contents without reallocating — needed for correct bypass and DAW transport-seek behavior
- [x] JUCE: Override `processBlockBypassed()` in `PluginProcessor.cpp` to copy each input channel to its output (true bypass); also call `reset()` on all effects so stale filter/delay state does not bleed through when the effect is re-engaged
- [x] JUCE: Run pluginval against the VST3 and AU builds before DAW testing — `brew install --cask pluginval` then `pluginval --validate-in-process Distant-Echo.vst3`; fix any reported contract violations
- [x] JUCE: Run `auval -v aufx Anoe Anoe` on the AU build to pass Apple's Audio Unit validation before loading in Logic or GarageBand
- [x] JUCE: Test preset save/restore round-trip — pluginval's "Plugin state" test exercises `getStateInformation`/`setStateInformation` at every sample rate and block size; all passed
- [x] JUCE: Stress-test delay time automation — Catch2 test (`Delay: rapid time automation produces no NaN/Inf or runaway`) sweeps `dl_time` from 1 ms to 2000 ms across 200 blocks at 0.7 feedback; confirms no NaN, Inf, or amplitude > 100
- [x] VCV Rack: Add `onReset()` override to both `OverdriveModule` and `DelayModule` — call `effect.prepare(APP->engine->getSampleRate(), 1)` so right-click → Initialize zeroes filter history and the delay buffer (params reset automatically by the Rack framework)
- [x] VCV Rack: Remove the `effect.prepare(44100.0, 1)` call from both module constructors — `onSampleRateChange` is always called before the first `process()`, so the constructor call double-allocates the delay buffer at the wrong rate
- [x] JUCE: Add display text formatting to the Delay Time parameter — set a `valueToTextFunction` on `dl_time` in `createParameterLayout()` so knob text box shows "300 ms" instead of the raw float "300.000"
- [x] Both: Clamp the output sample in `processBlock()` (JUCE) and `process()` (VCV Rack) after calling `effect.process()` — clamp to `[-1.0, 1.0]` in JUCE and `[-12.0, 12.0]` V in VCV Rack to prevent runaway feedback at extreme settings from exceeding DAW clip limits or Rack voltage standards
- [x] Overdrive: Smooth `mid` and `presence` parameter changes to eliminate biquad-state-reset transients during automation — replaced "redesign + state reset" with `SmoothedValue midSmoothed/presSmoothed`; biquads now redesign without state reset so the filter settles naturally; golden tests still pass
- [x] Overdrive: Replace `std::tanh` / `std::atan` per oversampled sample in `SoftClip` and `Asymmetric` modes — added `fastTanh` ([7/6] rational Padé, max error < 5e-4); precompute `tanh(g)` scale once per original-rate sample into `scaleBuf`; `atan` uses float overload instead of double cast; all 15/15 golden tests pass
- [x] Overdrive: Removed the dead `float prevMidComp = 0.0f` field from `libs/effects/Overdrive.h`
- [x] JUCE: Implement `releaseResources()` — call `reset()` on all overdrive, cabinet, and delay instances so filter and delay-line state is zeroed when the host deactivates the plugin; while the JUCE contract guarantees `prepareToPlay()` before the next `processBlock()`, clearing state on release makes the lifecycle explicit and guards against any host that skips the guarantee.
- [x] JUCE: Make `getTailLengthSeconds()` dynamic — replace the hardcoded `return 2.0` with `return *apvts.getRawParameterValue("dl_time") / 1000.0` (an atomic read, safe from any thread); at short delay times (e.g. 1 ms) the host currently waits a full 2 seconds after playback stop to render the tail, which causes unnecessary latency before the transport can be repositioned.
- [x] VCV Rack: Reset `bypassHigh` to `false` in `onReset()` in both `OverdriveModule` and `DelayModule` — if the bypass gate is high when the user right-clicks → Initialize, `bypassHigh` stays `true` and the module continues bypassing until the gate goes low; add `bypassHigh = false;` to both `onReset()` overrides.
- [x] VCV Rack: Zero tap/clock accumulated state in `DelayModule::onReset()` — `tapPrimed`, `tapHigh`, `tapTimeMs`, `tapSampleCount`, `clkPrimed`, `clkHigh`, `clkTimeMs`, and `clkSampleCount` are not cleared by the current `onReset()`, so the first clock rising edge after right-click → Initialize computes its interval including time that elapsed before the reset, potentially setting the wrong delay time; reset all eight fields to their zero/false defaults in `onReset()`.

## 12. Configurable Distortion Types

- [x] Define a `DistortionType` enum in `Overdrive.h` covering the target modes: `HardClip`, `SoftClip`, `Foldback`, `Asymmetric`, `Bitcrush` (or similar)
- [x] Refactor `Overdrive::process()` to dispatch through the selected `DistortionType` — a `switch` on the current mode choosing the appropriate waveshaper
- [x] Implement each waveshaper in Python first (`python/overdrive.py`) and generate golden WAVs for each mode before touching the C++
- [x] Expose the distortion type as a new parameter in the JUCE plugin (`AudioProcessorValueTreeState` choice parameter) and map it to a drop-down or radio buttons in the editor
- [x] Expose the distortion type as a new input port (or a menu item via `configSwitch`) in the VCV Rack module
- [x] Add Catch2 test cases for each distortion mode (silence-in/silence-out, no NaN/Inf at extreme drive, expected asymmetry/fold behavior)
- [x] Run `make compare` after each mode is implemented to confirm C++ output stays within 5e-4 of the Python golden

## 13. Overdrive — Aliasing and Clipping Quality

The current hard clipper produces strong aliasing at high drive and has an unrealistic infinite-slope knee. The circuit emulations are alias-free and use smooth diode/tube transfer curves. These items close that gap.

### 13a. Oversampling (highest priority — most audible artifact)

- [x] Add an `Oversampler` utility class in `libs/effects/Oversampler.h` — 4× polyphase FIR upsample/downsample with a linear-phase antialiasing filter (Kaiser-windowed, ≥ 80 dB stopband attenuation); implement and validate in Python (`python/oversampler.py`) first
- [x] Integrate `Oversampler` into `Overdrive::prepare()` / `process()` — upsample the input block, run the DC block + gain + waveshaper at 4× rate, downsample before the tone filter; confirm aliasing products drop below −80 dBFS for a 5 kHz sine at drive=1.0
- [x] Add a Catch2 test: at full drive, a 440 Hz sine input must produce no spectral component above 20 kHz that is > −80 dBFS after downsampling (note: test uses 6 kHz — see `docs/lessons-learned.md` §13a for why 440/5000 Hz cannot be used)
- [x] Keep a compile-time `OVERDRIVE_OVERSAMPLING` factor (default 4) so it can be reduced to 1 for CPU-constrained VCV Rack patches without changing the API

### 13b. Realistic waveshapers (smooth diode/tube curves)

- [x] Add a `tanhSoftClip(x, gain)` waveshaper: `y = tanh(gain·x) / tanh(gain)` — normalised so unity gain at the knee; prototype and validate golden WAVs in Python; use as the default `SoftClip` mode from section 12
- [x] Add a two-stage asymmetric soft clipper that models a silicon input diode (+) and germanium return diode (−): positive half uses `tanh(k·x)`; negative half uses `atan(k·x)` (softer, more compressed) — produces stronger even-order harmonics than the symmetric tanh
- [x] Add a polynomial soft clipper (`x − x³/3`) for the `HardClip` mode knee: blends from linear through the transition zone before hard-limiting at ±1; this is the Chebyshev waveshaper used in many Strymon circuit models
- [x] Prototype all three in `python/overdrive.py`, generate golden WAVs with `make golden`, then implement in C++ and confirm ≤ 5e-4 max error with `make compare`

### 13c. Pre-emphasis / de-emphasis (frequency-shaped clipping)

- [x] Add a shelving high-shelf pre-emphasis filter (+6 dB above 700 Hz) applied *before* the gain stage and a matched de-emphasis filter (−6 dB above 700 Hz) applied *after* the clip — this concentrates clipping in the mids while preserving low-end fundamentals; matches the Tube Screamer input network
- [x] Implement in Python first (`python/overdrive.py --mode ts-style`); validate that the combined pre/de-emphasis chain is unity gain at low drive
- [x] Expose `ClipShape` enum: `Flat` (current behavior), `MidFocus` (Tube Screamer pre/de-emphasis), `BrightFocus` (boost above 3 kHz before clip)

### 13d. Mid-frequency character controls

- [x] Add a parametric mid-boost biquad (peaking EQ) inserted after the tone filter — center frequency 800 Hz, bandwidth 1.5 octaves, boost range −6 dB to +10 dB; default 0 dB (flat); this is the "character" EQ that separates amp-in-a-box tones from each other
- [x] Add a `presence` parameter (0–1 → 0 to +8 dB high shelf at 4 kHz) applied after the level scalar — adds sparkle without harshness
- [x] Expose both parameters in the JUCE plugin (`AudioProcessorValueTreeState`) and VCV Rack module (knob + CV); update `CLAUDE.md` if the parameter count changes

### 13e. Dynamic response (pick sensitivity)

- [x] Add an input-level follower (peak detector, 1 ms attack / 100 ms release) that gently reduces the pre-amp gain (−3 dB to 0 dB) as the input envelope rises — this mimics the compression that occurs in a tube stage near saturation and causes the effect to "open up" on soft playing; prototype the envelope follower in `python/overdrive.py`
- [x] Add a Catch2 test: a 0 dBFS impulse followed by a −20 dBFS tone must produce less gain reduction in the steady-state tone than during the impulse peak

## 14. Delay — Audio Quality and Feature Parity

The current delay has integer-only delay, no modulation, and an abrupt read-pointer jump on time changes. The tape and BBD modes have fractional interpolation, wow/flutter, tape saturation, and ducking. These items close that gap.

### 14a. Fractional-delay interpolation (highest priority — prerequisite for modulation)

- [x] Replace the integer read-pointer in `Delay::process()` with a **linear interpolation** read: `wet = (1−frac)·buf[n] + frac·buf[n+1]` where `frac` is the sub-sample fractional part of `delaySamples`; validate in Python (`python/delay.py --interp linear`) and generate new golden WAVs
- [x] Upgrade to **4th-order Lagrange interpolation** for the "Tape" mode — better frequency response up to Nyquist; keep linear interpolation as the default for the "Digital" mode (lower CPU)
- [x] Add a Catch2 test: set delay to a non-integer number of samples and confirm the output does not click or jump when time changes smoothly by ±1 sample per block

### 14b. Smooth time changes (crossfade on pointer jump)

- [x] Implement a read-pointer crossfade on `setTimeMs()` while running: when the target delay changes, ramp the old read pointer out over 10 ms (fade out) while ramping the new read pointer in (fade in) — eliminates the pitch glitch on abrupt time edits; implement `pendingTimeSamples` + `crossfadeCounter` state in `Delay.h`
- [x] Add a Catch2 test: changing delay time by 50% mid-stream must produce no sample with |value| > 2× the pre-change peak (i.e., no click spike)

### 14c. Wow and flutter (tape speed modulation)

- [x] Add two LFO parameters to `Delay`: `wowRate` (0.05–2 Hz) and `wowDepth` (0–10 ms peak deviation); implement as a sine LFO that offsets the fractional read pointer each sample — requires fractional interpolation from 14a
- [x] Add `flutterRate` (3–12 Hz) and `flutterDepth` (0–2 ms); flutter uses a band-limited noise LFO (sum of two sines at slightly different rates) rather than a pure sine
- [x] Implement in Python first (`python/delay.py --wow 0.5 --wow-depth 4`); generate golden WAVs; confirm C++ output is within 5e-4 of Python reference
- [x] Expose `wowRate`, `wowDepthMs`, `flutterRate`, and `flutterDepthMs` as a single "Mod Depth" knob (wow + flutter at a fixed ratio) in JUCE and VCV Rack

### 14d. Tape saturation in feedback path

- [x] Replace the single-pole LP in the feedback path with a **tape saturation stage**: apply `tanh(satDrive · lpState) / tanh(satDrive)` after the LP filter — models magnetic tape saturation; with `satDrive` ≈ 2.0, this limits runaway build-up while adding warmth; prototype in `python/delay.py --tape-sat`
- [x] Add `tapeAge` parameter (0–1) that simultaneously lowers the feedback LP cutoff (4 kHz → 1.5 kHz) and increases saturation drive (2 → 5) — models an aging tape machine; exposed as `dl_tape_sat` toggle + `dl_tape_age` knob in JUCE and TAPE_SAT_PARAM / TAPE_AGE_PARAM in VCV Rack
- [x] Add a Catch2 test: at feedback=0.94 with tape saturation enabled, a sustained sine input must not produce any output sample > 1.5 after 5000 samples (saturation caps runaway)

### 14e. Ducking

- [x] Add an input-level envelope follower (5 ms attack / 500 ms release) that attenuates the wet/mix gain when the dry input is above a threshold — the delay signal "ducks" while playing and emerges in the gaps; this is the signature feature of the "Duck" delay mode
- [x] Parameters: `duckThreshold` (−30 to 0 dBFS) and `duckDepth` (0–1, where 1 = fully silenced when input is present); exposed as `dl_duck_threshold` + `dl_duck_depth` knobs in JUCE and DUCK_THRESHOLD_PARAM / DUCK_DEPTH_PARAM in VCV Rack
- [x] Add a Catch2 test: with duck enabled at depth=1.0, confirm wet output is < −60 dBFS while a loud dry signal is present, and rises to expected level within 600 ms of silence

### 14f. Diffusion (pre-delay allpass network)

- [x] Add an optional pre-diffusion stage before the main delay line: four allpass filters in series (delays 11 ms, 17 ms, 23 ms, 31 ms; coefficient 0.5) that smear transients and make repeats sound more "bloom" like a tape or reverb-delay hybrid
- [x] Implement in Python (`python/delay.py --diffuse`) and generate golden WAVs; confirm C++ within 5e-4
- [x] Expose as a `diffusion` parameter (0 = bypassed, 1 = full allpass chain) in JUCE and VCV Rack; add to the panel SVG as a "Diff" knob

### 14g. Self-oscillation mode

- [x] Expose a `selfOscillate` bool parameter that unlocks feedback above 0.95 (up to 1.02) when enabled — with tape saturation from 14d active, the output saturates rather than blowing up; this is the infinite-sustain "freeze" mode on premium delays
- [x] Guard the mode: if `selfOscillate=true` and tape saturation is disabled, clamp feedback at 0.98 rather than 1.02 to prevent unbounded growth
- [x] Add a Catch2 test: with selfOscillate + tape saturation at feedback=1.0, 2000 samples of sine input then silence must produce output that stays within ±2.0 for 10000 subsequent samples

### 14h. Logical extensions

- [ ] **Feedback tone control** — Replace the fixed 4 kHz LP cutoff with a user-facing `fbTone` knob (500 Hz–8 kHz, logarithmic scale) driven by a `SmoothedValue` (20 ms ramp) so knob turns produce no zipper noise; prototype in `python/delay.py --fb-tone 2000`; generate golden WAVs for 1500 Hz and 6000 Hz; expose as `dl_fb_tone` `AudioParameterFloat` in JUCE and `FB_TONE_PARAM` knob + `FB_TONE_INPUT` CV jack in VCV Rack; update `docs/Delay.md` parameter table.
- [ ] **Pre-delay** — Add `preDelayMs` (0–100 ms) as a short circular buffer before the feedback loop so the first repeat is offset from the dry signal without changing the repeat interval; prototype in Python; generate golden WAVs for `preDelay=20` and `preDelay=50`; expose as `dl_pre_delay` `AudioParameterFloat` in JUCE and `PRE_DELAY_PARAM` + `PRE_DELAY_INPUT` in VCV Rack; add a Catch2 test confirming the first output repeat arrives at `(preDelayMs + time_ms) × sampleRate / 1000` samples after onset.
- [ ] **Spillover on bypass** — When `dl_bypass` is toggled true, mute the dry input from the write path and let the feedback tail flush naturally rather than calling `delay.reset()`; add a `bypassFading` bool to `Delay.h`; in JUCE `processBlockBypassed()`, call `delay.process(silence, N)` until the tail falls below −60 dBFS (tracked via `EnvelopeFollower`), then call `delay.reset()`; add a Catch2 test confirming output is non-zero for at least `time_ms × 0.5` ms after bypass engages.
- [ ] **Haas width control** — For `processStereo()` in independent mode (not ping-pong), add a `haasWidth` parameter (0–1) that scales the right-channel delay time from `time × 1.0` (width=0, mono) to `time × 1.02` (width=1, current default) so users can dial back the stereo spread; expose as `dl_haas_width` in JUCE and `HAAS_WIDTH_PARAM` in VCV Rack; add a Catch2 test confirming that at width=0 both stereo output channels are sample-identical.
- [ ] **Feedback high-pass filter** — Add `feedbackHPEnabled` bool and `feedbackHPCutoff` (20–200 Hz, default 40 Hz) that remove low-end accumulation in the feedback path when bass or kick content passes through at high feedback settings; implement using `OnePoleHP` from TODO 26b; expose as `dl_fb_hp` toggle + cutoff knob in JUCE and `FB_HP_PARAM` + `FB_HP_CUTOFF_PARAM` in VCV Rack; add a Catch2 test confirming a 40 Hz sine at feedback=0.95 does not accumulate beyond 2× amplitude after 2000 samples.
- [ ] **Rate-limited feedback step response** — Wrap the internal feedback coefficient in `SmoothedValue feedbackSmoothed` (20 ms ramp) so automation jumps (e.g., 0.0 → 0.9 in one block) do not click when the delay buffer contains high-energy content; add a Catch2 test confirming no output sample exceeds 2× the pre-change peak during a 0→0.9 feedback step; update the self-oscillation Catch2 test to verify at the settled `feedbackSmoothed` value.
- [ ] **Catch2 stereo independence test** — Add a test in `tests/test_delay.cpp` running `processStereo()` in independent mode with a left-only impulse and confirming the right output is below −60 dBFS for the first `int(time_ms × 0.9 × sampleRate / 1000)` samples; verifies the two delay lines share no state before the first right-channel repeat arrives.
- [ ] **Stereo golden WAV coverage** — Add stereo golden WAVs for diffusion, self-oscillation, and tape saturation by passing `--stereo` to `wav_compare` for each mode; add the output files to `tests/golden/`; update `compare.py` to diff stereo golden WAVs channel-by-channel at the same 5e-4 tolerance; currently only mono golden WAVs exist for these modes.
- [ ] **Output soft-clip bypass flag** — Add a compile-time `DELAY_SOFTCLIP_OUTPUT` flag (default 1) that disables the `tanh` output soft-clip in the feedback path when CPU is critical (VCV Rack patches at ≥ 96 kHz); when 0, substitute a hard clip at ±1.0; document the CPU vs. artifact trade-off in `docs/Delay.md` and in a comment above the flag in `Delay.h`.
- [ ] **Combined self-oscillation + tape saturation golden WAV** — Add `golden/delay_self_osc_tape.wav` with `selfOscillate=true`, `tapeSat=true`, `feedback=1.0`, `time_ms=200`; the two saturation paths are currently tested in isolation; a joint golden WAV confirms that Python and C++ agree on their interaction; add the file to `compare.py` with a 5e-4 tolerance.

### 14i. Unusual extensions

- [ ] **Lorenz attractor modulation** — Add `chaoticMod` bool that replaces the periodic sine wow LFO with a Lorenz attractor integrated per-sample: `ẋ = σ(y−x)`, `ẏ = x(ρ−z)−y`, `ż = xy−βz` (σ=10, ρ=28, β=8/3, Euler step 1/sampleRate); normalize x to ±`wowDepthMs`; seed initial state from a `chaoticSeed` float parameter (default 0.1, 0.1, 0) for reproducible golden WAVs; the modulation is deterministic but non-repeating; prototype in `python/delay.py --chaotic-mod`; generate golden WAVs.
- [ ] **Alternate-repeat polarity flip** — Add `flipPhase` bool that inverts the polarity of every other repeat by toggling `polaritySign` (±1) each time the write pointer completes one full `delaySamples` revolution; the deepening comb interaction between opposite-polarity repeats creates a metallic swell; prototype in `python/delay.py --flip-phase`; generate golden WAVs; add a Catch2 test confirming spectral energy at the comb null frequency is at least 12 dB below a reference no-flip run.
- [ ] **Pitch-tracking delay time** — Add `pitchTrack` bool that estimates the input pitch every 30 ms via a zero-crossing counter and sets `time_ms = 1000.0 / detectedHz`, making the delay resonate at the input fundamental; hold the last valid time during silence or noise; prototype in Python with a 440 Hz / 220 Hz alternating test signal; generate golden WAVs; guard against divide-by-zero and pitches below 20 Hz.
- [ ] **Brownian motion flutter** — Replace the current flutter LFO (two slightly-detuned sines) with a Wiener-process approximation: integrate white noise through a 2 Hz one-pole LP, normalize to ±`flutterDepthMs`, and subtract the accumulated mean every 1000 samples to prevent DC drift; add a `brownianFlutter` bool (default false, existing sine LFO preserved); prototype in Python with `seed=42` for reproducibility; generate golden WAVs.
- [ ] **Thermal drift model** — Add `thermalDrift` parameter (0–1) that modulates `tapeAge` from 0 to `thermalDrift` using a `sin²` LFO driven by a sample counter (period ≈ 60 seconds at 48 kHz); at startup the tone is brighter; after 60 seconds it settles to full warmth; expose as `dl_thermal_drift` in JUCE; the sample counter resets on `reset()` so re-engaging bypass restarts the warm-up cycle; add a comment in `Delay.h` explaining why the modulation period is non-smoothed.
- [ ] **Spectral phase scrambler** — Add `smear` parameter (0–1) that applies a 64-bin real-FFT to the feedback signal every 64 samples, rotates each bin's phase by `smear × π × U(−1,1)` using a seeded `std::mt19937`, and inverse-FFTs before writing back to the delay path; at `smear=0` the signal is unmodified; at `smear=1` all phase coherence is destroyed within each frame, turning repeats into an inharmonic noise haze; seed via `smearSeed` int parameter for golden WAVs; prototype with `numpy.fft`; note the 64-sample latency in `docs/Delay.md`.
- [ ] **DNA-sequence rhythmic gate** — Add `dnaGate` bool that gates the delay output against a hardcoded 32-nucleotide excerpt from the human IGF2 promoter (A/T/G/C); map A→quarter note, T→eighth, G→sixteenth, C→rest at the BPM implied by `time_ms`; gate transitions use 5 ms linear ramps; the sequence repeats every 32 notes; expose as `dl_dna_gate` bool in JUCE only; intended as a creative rhythmic modifier.
- [ ] **Magnetic dropout simulation** — Add `magneticRust` parameter (0–1) that zeroes individual samples in the feedback path with probability `magneticRust × 3e-5` per sample (≈ 1.4 dropouts/second at full setting), replacing each zeroed sample with a 0.5 ms burst of 6 kHz LP-filtered noise to model a tape splice dropout; expose `deterministicSeed` int parameter for reproducible golden WAVs; non-deterministic by default (`std::random_device`); expose in JUCE.
- [ ] **Turing-machine feedback register** — Add `turingLength` (2–32 steps) and `turingProbability` (0–1) parameters that implement a shift register clocked by the delay write pointer every `delaySamples` samples: shift left, XOR the MSB back, and flip a random bit with probability `turingProbability`; use the register parity to toggle `polaritySign` in the feedback accumulation, creating sequences with period up to `2^turingLength` samples; expose in VCV Rack as `TURING_LENGTH_PARAM` knob and `TURING_PROB_PARAM` knob.
- [ ] **Listener position room model** — Add `roomEnabled` bool, `roomWidthM` (1–20 m), and `listenerPos` (0–1) that compute two extra delay taps: left-wall reflection at `roomWidthM × listenerPos / 343 × 1000 ms` and right-wall reflection at `roomWidthM × (1 − listenerPos) / 343 × 1000 ms`, each attenuated by distance and low-passed at 3 kHz; the taps feed the wet sum independently of the main delay line; prototype in Python; expose in JUCE only.
- [ ] **Geomagnetic noise injection** — Add `geomagneticNoise` parameter (0–1) that mixes a scaled version of a hardcoded 1024-sample Kp-index-derived geomagnetic disturbance waveform (sourced from NOAA SWPC, normalized to ±1, looped) into the delay buffer as additive noise; at `geomagneticNoise=0.001` the effect is sub-audible but shifts the spectral character of self-oscillation; intended as an easter egg; expose in JUCE behind a hidden label click; document in `docs/Delay.md` under "Experimental Features."

## 15. Stereo Support and Infrastructure

These items are prerequisites for ping-pong delay, stereo overdrive, and future reverb/chorus effects.

- [x] Extend the `Effect` base class with an optional `processStereo(float* left, float* right, int numSamples)` virtual — default implementation calls `process(left, numSamples)` and copies left→right (mono-to-stereo passthrough); stereo effects override it
- [x] Add `Delay::processStereo()` with ping-pong mode: odd repeats appear on the right channel, even repeats on the left — requires two delay lines sharing a single feedback path; expose as a `pingPong` bool parameter
- [x] Add `Delay::processStereo()` stereo-independent mode: two independent delay lines with slightly different times (e.g., right = left × 1.02) for a wide, lush stereo field
- [x] Update both the JUCE plugin (stereo bus layout) and VCV Rack modules (add R IN / R OUT ports) to use `processStereo()` when a stereo signal is present
- [x] Update all golden-output tests to generate and validate stereo WAV files for the stereo modes; add a `--stereo` flag to `wav_compare`

## 16. Overdrive — Advanced Character and Dynamics

### 16a. Bias / operating-point shift

- [x] Add a `bias` parameter (−0.5 to +0.5, default 0) that applies a DC offset to the signal immediately before the waveshaper: `x_biased = x + bias`. A non-zero bias shifts the operating point so positive and negative half-cycles clip at different thresholds, generating strong even-order harmonics from symmetric waveshapers (`HardClip`, `SoftClip`) that would otherwise produce only odd harmonics — simulating the effect of a transistor whose quiescent bias has drifted. Prototype in `python/overdrive.py`; generate golden WAVs for `bias=±0.3` with `SoftClip`; confirm C++ within 5e-4.

### 16b. Power supply sag

- [ ] Add a `sag` parameter (0–1, default 0) implementing a slow peak-envelope follower (50 ms attack / 500 ms release) on the rectified pre-clip signal. Scale the waveshaper's ±1 clip ceiling down by `1 / (1 + sag × envState)` — as the envelope rises the ceiling drops, causing loud passages to sound compressed and harmonically richer while quiet passages open up cleanly. This models the power supply voltage drooping under load in a tube amp (Marshall Plexi / Fender Deluxe sag character). Prototype in Python; expose in JUCE and VCV Rack.

### 16c. Gated fuzz

- [ ] Add a `GatedFuzz` `DistortionType`: apply a comparator gate — set samples with `|x| < T` (where `T = 0.15`) to zero, then hard-clip surviving samples to ±1. The resulting dead zone around every zero-crossing creates the abrupt, "velcro" stutter of a voltage-starved germanium circuit (Univox Super-Fuzz / MXR Blue Box). Unlike `HardClip`, there is no smooth knee — the output is either zero or ±1 with nothing in between. Prototype in Python first; generate golden WAVs; add to `wav_compare` and `compare.py` with a per-file tolerance override (mode is discontinuous).

### 16d. Octave-up fuzz

- [ ] Add an `OctaveFuzz` `DistortionType`: full-wave rectify the input (`|x|`) to fold the negative half-cycle onto the positive, then apply a 200 Hz HP biquad to remove the DC component that rectification introduces, then hard-clip to ±1. Full-wave rectification doubles the fundamental frequency in the output spectrum — a 440 Hz input yields a dominant 880 Hz tone — faithfully reproducing the octave-up pitch effect of a Roger Mayer Octavia or Foxx Tone Machine. Prototype in Python; generate golden WAVs; confirm the 880 Hz component exceeds the 440 Hz component by ≥ 6 dB in the golden output spectrum.

### 16e. Parallel clean blend

- [ ] Add a `blend` parameter (0–1, default 0) that mixes the original dry input — delay-compensated by the oversampler's group delay (≈ 16 samples at 48 kHz) using a short circular buffer — back with the post-clip wet signal: `out = (1 − blend) × wet + blend × dry_delayed`. This preserves pick transients and low-end definition at any drive level, equivalent to NY-style parallel compression applied to distortion. Prototype in Python with exact sample-accurate delay compensation; generate golden WAVs for `blend=0.3` and `blend=0.7` at full drive; expose in JUCE and VCV Rack.

### 16f. Noise gate

- [ ] Add a `gateThreshold` parameter (−80 to −20 dBFS, default −80 = effectively off) with a two-stage envelope follower: 1 ms attack to track the input RMS, 30 ms hold after the signal drops below threshold, then 80 ms linear release to silence. When the gate closes, multiply the output by a smoothly falling gain coefficient rather than snapping to zero. Applied before the output level scalar so it operates on the post-clip signal. Prototype in Python with the same coefficient design; generate golden WAVs for `gateThreshold=−30 dBFS`; expose in JUCE and VCV Rack.

### 16g. Speaker cabinet IR convolution

- [x] Add an optional `cabinetEnabled` bool (default false) that post-processes the output with a short FIR convolution (`N ≤ 256 taps`) stored in a generated header `libs/effects/CabinetIR_data.h`. Design the initial IR as a minimum-phase 1×12 open-back cabinet response using a minimum-phase reconstruction from a target magnitude curve (−6 dB/octave above 4 kHz, first-order resonance peak at 120 Hz). Use direct-form convolution for `N ≤ 256` taps (acceptable CPU at 48 kHz); bypass cleanly when disabled. Prototype the FIR design in Python; validate that the convolved output matches `scipy.signal.fftconvolve` within 5e-4; expose the toggle in JUCE and VCV Rack. Implemented as a standalone `CabinetIR` Effect subclass (not embedded in `Overdrive`); normalization uses `|H(1 kHz)| = 1.0` (passband unity-gain, not time-domain peak); both Python and C++ apply an explicit ±1.0 hard clip after convolution to stay consistent with PCM-16 golden WAV behavior.

  Follow-on:
  - [x] Add a cabinet frequency-response plot (similar to `overdrive-tone-blend.png`) to `docs/img/` via `generate_docs_plots.py` — showing the +6 dB resonance at 120 Hz and −6 dB/octave rolloff above 4 kHz. All three cabinet types shown on one plot (`cabinet-ir-response.png`).
  - [x] Add a VCV Rack panel CKSS toggle for Cabinet IR (previously right-click only); `res/Overdrive.svg` and `src/OverdriveModule.cpp` updated. Toggle at x=25.4, y=96 mm with "CAB" label.
  - [x] Support multiple cabinet presets (1×12 open-back, 4×12 closed-back, 1×12 combo) via `CabinetType` enum; separate `CabinetIR_data_4x12.h` and `CabinetIR_data_combo.h` headers generated from `python/cabinet_ir.py --type 4x12|combo`; exposed as `od_cabinet_type` `AudioParameterChoice` in JUCE (3rd combo row in OD section) and via right-click context menu in VCV Rack.
  - [ ] For user-loadable IRs (arbitrary WAV files), replace the fixed-array direct-form convolver with an overlap-add FFT convolver; this also removes the 48 kHz sample-rate assumption baked into the current IR.
  - [x] The current IR is designed and normalized at 48 kHz. `CabinetIR::prepare()` now resamples the selected IR to the current sample rate using linear interpolation so resonance and rolloff frequencies stay correct at 44.1, 48, and 96 kHz.
  - [x] Add golden WAV test coverage for the 4x12 and combo cabinet types: extend `generate_golden.py` to process a reference sine through each cabinet type and save the output; add entries in `compare.py` at the same 5e-4 tolerance; currently only the 1x12 type has golden WAV coverage in the test pipeline. Added 4 new golden cases (4x12_medium/hard, combo_medium/hard); `wav_compare` extended with `--type` flag; `test_cabinet.cpp` expanded with 15 Catch2 tests covering all three types (NaN/Inf, 1 kHz unity gain, 10 kHz attenuation thresholds, rolloff differentiation, resonance peaks, type-switching history reset). 33/33 golden WAVs pass; 15/15 unit tests pass.
  - [ ] Add a Catch2 test to `tests/test_cabinet.cpp` verifying that the inherited `CabinetIR::processStereo()` (default: calls `process(left, N)` then copies L→R) produces identical left and right output samples for the same mono input; a bug where both channels share one history buffer would cause them to diverge after the first call; run for all three cabinet types to confirm they don't accidentally override `processStereo()` incorrectly.
  - [ ] Add a Catch2 test verifying that the 1x12 cabinet's 10 kHz attenuation ratio at 44100 Hz is within 15% of the same ratio at 48000 Hz; the `buildActiveIR()` linear-interpolation resampling is intended to preserve rolloff and resonance frequencies across sample rates — a regression there passes existing NaN/Inf tests but produces incorrect tone; this is the "sample-rate frequency shift" check mentioned in the §16g follow-ons for the resampling feature.

### 16h. Harmonic exciter

- [ ] Add an `exciter` parameter (0–1, default 0) implementing an Aphex Aural Exciter model: split the signal with a 3 kHz 2nd-order HP biquad, drive the separated high-frequency band through a soft clipper at a low threshold (`0.1 × exciter`), then sum the synthesized HF harmonics back with the main signal scaled by `exciter × 0.3`. The HP filter + soft clip generates new harmonics above 6 kHz from whatever content exists above 3 kHz, adding air and definition without boosting the fundamental. Applied after the presence shelf, before the output level scalar. Prototype in Python; generate golden WAVs for `exciter=0.5` and `exciter=1.0`.

### 16i. Logical extensions

- [ ] **Oversampling factor runtime control** — Expose the compile-time `OVERDRIVE_OVERSAMPLING` constant as a runtime `oversamplingFactor` choice parameter (1×, 2×, 4×) in JUCE (`AudioParameterChoice`) and via right-click context menu in VCV Rack; `prepare()` reinitializes `Oversampler` when the factor changes mid-session; add a Catch2 test confirming 1× and 4× agree within 1e-2 max error for a 440 Hz sine at low drive, and that a mid-stream factor switch produces no output sample > 2× the pre-switch peak.
- [ ] **DistortionType crossfade on switch** — When `DistortionType` changes mid-stream, crossfade both waveshaper outputs over 10 ms using `SmoothedValue typeBlend`; evaluate the current and previous waveshaper each oversampled sample and blend during the transition; store `prevType` state; expose the ramp duration as compile-time `OVERDRIVE_TYPE_XFADE_MS`; add a Catch2 test confirming no output sample exceeds 2× the pre-change peak during a type switch.
- [ ] **True stereo overdrive mode** — Add `Overdrive::processStereo()` override that processes L and R channels independently through separate oversampler and waveshaper instances, allowing per-channel `bias` values (`biasL`, `biasR`) and independent envelope followers for pick sensitivity; expose `od_bias_lr_link` bool in JUCE (default true) and `BIAS_L_PARAM` / `BIAS_R_PARAM` in VCV Rack; useful for stereo rigs where each channel has a different pickup character.
- [ ] **Pre-gain boost stage** — Add a `boost` parameter (0 to +20 dB, default 0) inserted before the drive stage as a gain scalar with `SmoothedValue boostSmoothed`; allows level-matching when switching distortion types or input sources without touching the master level; expose as `od_boost` `AudioParameterFloat` in JUCE and `BOOST_PARAM` + `BOOST_INPUT` CV jack in VCV Rack; add a Catch2 test confirming +6 dB boost raises small-signal (below-clip) output amplitude by at least 3 dB.
- [ ] **Post-clip dirt EQ** — Add a `dirtCut` parameter (0–1, default 0 = flat) applying a high-shelf attenuation (0 to −12 dB above 6 kHz) after the waveshaper and before the tone blend; removes harshness from `HardClip` and `Foldback` without lowering overall level; design as `BiquadFilter::designHighShelf` with gain `−12 × dirtCut` dB and fc=6 kHz; expose as `od_dirt_cut` in JUCE and `DIRT_CUT_PARAM` in VCV Rack; generate golden WAVs for `dirtCut=0.5` and `dirtCut=1.0`.
- [ ] **Drive SmoothedValue audit** — Confirm in `Overdrive.h` that the `drive` parameter is fed through a `SmoothedValue`; if not, add `SmoothedValue driveSmoothed` with a 20 ms ramp per the project convention in `CLAUDE.md`; add a Catch2 test that a drive step from 0.0 to 1.0 between two consecutive blocks produces no output sample > 2× the post-step steady-state peak (zipper noise test).
- [ ] **A/B parameter snapshot** — Add `presetSlotAB` bool (A=false / B=true) and `captureSlot()` method that stores a complete snapshot of all Overdrive float parameters into an internal struct; expose as an "A/B" toggle button in the JUCE editor (clicking toggles the slot and recalls the snapshot via APVTS) and as a `PRESET_AB_PARAM` CKSS toggle in VCV Rack; allows instant comparison of two drive settings during a recording session without relying on DAW automation.
- [ ] **Input trim control** — Add an `inputTrim` parameter (−12 to +12 dB, default 0) inserted before the `boost` stage as a gain-only scalar with `SmoothedValue`; normalizes different guitars, pickups, and active/passive instruments before the drive chain so the knob settings remain consistent across sources; expose as `od_input_trim` in JUCE and `INPUT_TRIM_PARAM` + `INPUT_TRIM_INPUT` in VCV Rack; update `docs/Overdrive.md` signal-flow diagram.
- [ ] **Bias Catch2 spectral test** — Add a test in `tests/test_overdrive.cpp` verifying that `SoftClip` with `bias=0.3` produces a stronger 2nd-harmonic (880 Hz) component than `bias=0.0`; apply a 440 Hz sine at drive=0.5, process 4096 samples, compute FFT, and confirm the 880 Hz bin is at least 6 dB louder in the `bias=0.3` run; this operationalizes the claim in §16a that a non-zero bias generates strong even-order harmonics from symmetric waveshapers.
- [x] **Cabinet IR validation test** — Implemented in `tests/test_cabinet.cpp` (not `test_overdrive.cpp`): `"CabinetIR: 1 kHz passband gain is near unity for all cabinet types"` verifies ±1 dB of unity at 48 kHz for all three types; `"CabinetIR: resonance peak is louder than 10 kHz rolloff for each type"` confirms the 120/80/180 Hz resonance produces at least 50% more RMS than the 10 kHz rolloff region, equivalent to >+14 dB — well above the +4 dB threshold; `"CabinetIR: no NaN/Inf for all cabinet types at multiple sample rates"` covers 44100, 48000, and 96000 Hz for all types.
- [x] **Multiple cabinet presets** — Support 1×12 open-back (current), 4×12 closed-back, and 1×12 combo cabinet types via a `CabinetType` enum; generate separate IR data headers (`CabinetIR_data_4x12.h`, `CabinetIR_data_combo.h`) from `python/cabinet_ir.py` with appropriate target magnitude curves (4×12: resonance at 80 Hz, −3 dB/oct above 3 kHz; combo: resonance at 180 Hz, −6 dB/oct above 5 kHz); exposed as `od_cabinet_type` `AudioParameterChoice` in JUCE and via right-click context menu in VCV Rack.

### 16j. Unusual extensions

- [ ] **Cellular automaton waveshaper** — Add `CellularAutomaton` `DistortionType` that maps each input sample through Wolfram Rule 30: quantize to 8 levels (step 0.25, range −1 to +1), one-hot encode into an 8-bit state, evolve 3 CA generations using Rule 30's lookup table, then map the surviving active cell index back to a float via `(index − 3.5) / 3.5`; the waveshaper is discontinuous and deterministic, producing harmonic signatures that change non-linearly with amplitude; prototype in Python; generate golden WAVs; add to `compare.py` with a relaxed 1e-3 tolerance.
- [ ] **Microtonal bias drift** — Add `biasDrift` parameter (0–1) that drives `bias` through a Brownian random walk per block (`bias += N(0, biasDrift × 5e-5)`, clamped to ±0.5) so the operating point migrates over minutes without user intervention; expose the instantaneous bias offset as a CV output (`BIAS_CV_OUTPUT`, ±5 V = ±0.5 bias) in VCV Rack so other modules can track the drift; display the live bias value next to the Bias knob in JUCE via a `juce::Timer` at 10 Hz.
- [ ] **Retrograde block processing** — Add `retrograde` bool that reverses the pre-oversampled audio block (`std::reverse`), passes it through the waveshaper, then reverses again; within each block, transient attacks land at the block's end rather than its beginning, causing them to clip harder on the downswing into the block boundary; the artifact is subtle at 128 samples but striking at 2048; prototype in Python for block sizes 256 and 1024; generate golden WAVs with block size 512.
- [ ] **Phase distortion synthesis** — Add `PhaseDistortion` `DistortionType` inspired by Casio CZ synthesis: maintain a per-block phase accumulator; for each oversampled sample, warp the read phase via `readPhase = tablePhase + drive × |x| × tableSize` (wrap at tableSize), then output the sine table value at `readPhase`; the waveshape bends based on input amplitude without generating sub-harmonics or sum/difference tones; prototype in Python with a 2048-sample sine table; generate golden WAVs for `drive=0.5` and `drive=1.0`.
- [ ] **Feedback harmonic injection** — Add `harmonicFB` parameter (0–0.5, default 0) that phase-inverts the waveshaper output, delays it by exactly 1 sample, and mixes it back into the pre-clip signal at `harmonicFB` level: `x_modified = x + harmonicFB × (−prev_out)`; the recursive Volterra-series interaction alters the effective waveshaper curve per-sample, adding even-order harmonics without changing any other parameter; guard against instability with `SmoothedValue harmonicFBSmoothed`; prototype in Python; generate golden WAVs.
- [ ] **Stochastic resonance assist** — Add `srAssist` parameter (0–1) that injects Gaussian white noise at `srAssist × −60 dBFS` (≈ 0.001 amplitude) before the waveshaper, exploiting stochastic resonance to make distortion character appear more consistently at low drive settings where the signal is near but below the clipping knee; use a seeded `std::mt19937_64` for golden WAV reproducibility; expose as `od_sr_assist` in JUCE; note in `docs/Overdrive.md` that this is intentional noise, not a bug.
- [ ] **Symbiotic modulation (Overdrive ↔ Delay cross-wiring)** — When both Overdrive and Delay are active in the JUCE plugin, add `od_dl_link` `AudioParameterBool` that wires the Overdrive envelope follower output (pick sensitivity) to the Delay wow depth parameter; implement via `std::atomic<float> pickEnvOut` written in Overdrive's `processBlock()` and read in Delay's on the same audio thread (no lock needed); add a toggle button in the JUCE editor center divider; the interdependency cannot be replicated with separate plugin instances.
- [ ] **Xenharmonic foldback** — Add `XenharmonicFold` `DistortionType` using 31-ET fold thresholds: `thresholds[k] = k / 31.0f` for k=1..15; fold the signal at each threshold in sequence (when `x > thresholds[k]`, reflect: `x = 2 × thresholds[k] − x`); the 31 equal divisions produce harmonics closely aligned to just-intonation ratios, making the distortion sound "tonal but alien"; prototype in Python; generate golden WAVs for a 440 Hz sine; document in `docs/Overdrive.md` why 31-ET was chosen.
- [ ] **Geologic erosion** — Add `erosion` parameter (0–1) that performs a correlated random walk of `drive`, `bias`, and `mid` (in dB) every 1024 samples: each drifts by `N(0, erosion × 1e-4)` clamped to its valid range; `erosion=0` is completely stable; `erosion=0.5` produces a noticeable character shift every few minutes; add `resetErosion()` that snaps drifted parameters back to their last user-set values; expose as `od_erosion` in JUCE only; document as an experimental feature in `docs/Overdrive.md`.
- [ ] **Voltage-starved supply simulation** — Add `voltageStarve` parameter (0–1, default 0) that reduces the effective clip ceiling from ±1.0 to `±(1 − 0.7 × voltageStarve)` (floor ±0.3) while leaving the gain unchanged, forcing the signal harder into a lower ceiling; combine with `GatedFuzz` (16c) to reproduce the tone of a dying 9V battery in a germanium fuzz; add a Catch2 test confirming that at `voltageStarve=1.0` the output peak stays below 0.35; expose in JUCE and VCV Rack.
- [ ] **Cabinet IR user-loadable WAV** — Replace the fixed-array direct-form convolver in `CabinetIR` with an overlap-add FFT convolver using a 256-sample partition size; expose `CabinetIR::loadIR(float* irData, int irLen, double irSampleRate)` which resamples via polyphase FIR to the current audio rate before partitioning; add a "Load IR…" file browser button in the JUCE editor; enforce a maximum IR length of 8192 samples (170 ms at 48 kHz); this supersedes the 48 kHz sample-rate assumption baked into the current IR and the resampling follow-on in §16g.

## 17. Delay — Creative and Extended Features

### 17a. Reverse delay

- [ ] Add a `reverse` bool parameter. When enabled, fill a secondary ring buffer of length `time_ms` samples from the dry input; at the end of each fill cycle, play the buffer back in reverse into the wet path while filling the next cycle — producing the characteristic swell-before-the-note reversed echo. Apply a 10 ms equal-power crossfade at the segment boundary to eliminate the click between cycles. Requires the fractional interpolation from 14a. Prototype in `python/delay.py --reverse` with a seeded buffer so the golden WAVs are reproducible; generate golden WAVs; expose in JUCE and VCV Rack.

### 17b. Multi-tap delay

- [ ] Add a `taps` integer parameter (1–4, default 1) that activates up to three additional read pointers sharing the same circular buffer. The additional taps are positioned at `time_ms × [3/4, 1/2, 1/4]` — a dotted-quarter, plain half, and quarter subdivision of the primary tap — at fixed relative levels of −4, −8, and −12 dB. Each tap feeds into the wet sum independently; only the primary tap feeds the feedback path. Prototype in Python; generate golden WAVs for `taps=2` and `taps=4`; add multi-tap support to `wav_compare`; expose in JUCE and VCV Rack.

### 17c. Pitch-shifted feedback

- [ ] Add a `pitchShift` parameter (−12 to +12 semitones, default 0) that pitch-shifts the feedback signal by the given interval before writing it back into the delay buffer, using a granular pitch shifter: two Hann-windowed read heads (20 ms grain, 50% overlap) traversing the most-recently-written portion of the delay buffer at speed ratio `r = 2^(pitchShift/12)`, crossfaded continuously. Each successive repeat therefore sounds a semitone (or octave) higher or lower than the previous — recreating the Eventide H3000 "crystal" ascending-delay effect at +12 semitones. Prototype in Python; generate golden WAVs for `±12` and `+7` semitones; expose in JUCE and VCV Rack.

### 17d. BPM-synced delay time

- [ ] Add a `bpm` parameter (40–300 BPM, default 0 = free/manual) and a `subdivision` enum (`Quarter`, `DottedQuarter`, `Eighth`, `DottedEighth`, `EighthTriplet`). When `bpm > 0`, override `time_ms` with `60000 / bpm × subdivision_ratio` and smooth the resulting change through a 20 ms `SmoothedValue` to avoid clicks. In JUCE, expose as a secondary parameter with a free/sync mode toggle; in VCV Rack, accept a BPM CV input (1 V/oct, 0 V = 120 BPM via `exp2(v) × 120`). Prototype the BPM→ms computation in Python; add to the golden pipeline with deterministic BPM inputs.

### 17e. Karplus-Strong resonator mode

- [ ] Add a `resonator` bool parameter that reconfigures the delay into a Karplus-Strong plucked-string model: set feedback to 0.999, replace the 4 kHz LP with a two-sample averaging filter (`y[n] = 0.5 × (x[n] + x[n−1])`, equivalent to a half-sample LP), and excite the delay buffer with one block of white noise whenever `resonator` transitions false→true. The buffer then rings at `f = sampleRate / delaySamples` Hz and decays with a time constant determined by the averaging LP. Prototype in Python for delay times corresponding to A4 (440 Hz) and E2 (82 Hz); generate golden WAVs showing the exponential ring-down; expose in JUCE and VCV Rack.

### 17f. Shimmer

- [ ] Add a `shimmer` parameter (0–1, default 0) that blends a +1-octave pitch-shifted copy of the delay output (using the granular engine from 17c with `pitchShift = +12`) back into the delay input at level `shimmer × 0.5 × feedback`, creating a wash of octave-up repeats that grows denser with each pass — the signature Edge/U2 shimmer reverb texture. Requires 17c to be implemented first; the shimmer path bypasses the feedback LP so it does not darken over successive repeats. Prototype in Python; generate golden WAVs for `shimmer=0.5, feedback=0.6, time_ms=400`; expose in JUCE and VCV Rack.

### 17g. Glitch / granular scatter

- [ ] Add a `glitch` parameter (0–1, default 0) that, at randomised intervals drawn from `U(20, 500 − 480 × glitch)` ms, jumps the delay read pointer to a uniformly random position within the filled portion of the delay buffer. Surround each jump with a 5 ms linear fade-out/fade-in to suppress the click. At `glitch=1`, jumps occur every 20–40 ms; at `glitch=0.1`, every 450–500 ms. Use a seeded `std::mt19937` (seed exposed as a parameter for reproducible golden WAVs) so the output is deterministic. Prototype in Python with `random.seed`; generate golden WAVs; expose `glitch` and `glitchSeed` in JUCE and VCV Rack.

## 18. Overdrive — Signal Path Extensions

### 18a. Ring modulator

- [ ] Add a `ringFreq` parameter (1–5000 Hz, default 0 = bypassed) that multiplies each sample by `sin(2π × ringFreq × n / sampleRate)` applied immediately after the pre-amp gain stage, before the waveshaper. A maintained per-block phase accumulator (`ringPhase += 2π × ringFreq / sampleRate` per sample, wrapped at 2π) ensures phase continuity across audio callbacks. In the audio-frequency range (200–5000 Hz) the multiplication creates sum and difference tones against each partial in the input, producing metallic, inharmonic ring-modulation distortion; below 20 Hz it acts as a tremolo. Prototype in `python/overdrive.py`; generate golden WAVs for `ringFreq=250` and `ringFreq=800`; expose in JUCE and VCV Rack.

### 18b. Multiband / band-split distortion

- [ ] Split the pre-clip signal into three bands using two 4th-order Linkwitz-Riley crossovers (LR4 = two 2nd-order Butterworth stages in series) at 300 Hz and 3 kHz, giving low, mid, and high sub-bands that sum to a flat all-pass response. Apply the selected `DistortionType` waveshaper to each band independently with per-band drive multipliers `driveLow`, `driveMid`, `driveHigh` (all scaled from the global `drive` knob at ratios 0.5 ×, 1.0 ×, 0.7 × by default). Recombine the three bands before the tone blend. This allows the bass to remain clean while mids and highs saturate — matching the character of amp circuits that roll off low-frequency gain before the clip stage. Prototype in Python with `scipy.signal` crossover filters; generate golden WAVs; expose per-band drive ratios in JUCE.

### 18c. Envelope-driven waveshaper morphing

- [ ] Add a `dynamicClip` bool parameter that, when enabled, continuously crossfades the waveshaper output between `SoftClip` and `HardClip` based on the input envelope. Use the existing pick-sensitivity follower (13e) as the envelope source — `morphFactor = clamp(envState / 0.6, 0, 1)` — so soft playing produces tanh-smooth saturation and hard picking snaps toward hard clipping in real time. The blend is `(1 − morphFactor) × softOut + morphFactor × hardOut`, computed per sample with both waveshapers always evaluated. Prototype in Python; generate golden WAVs for a crescendo input (amplitude ramping 0→1 over 0.5 s); expose the toggle in JUCE and VCV Rack.

### 18d. Pickup loading / input impedance simulation

- [ ] Add an `inputLoading` bool (default false) that inserts a 2nd-order resonant filter before the DC block, modelling the interaction between a single-coil pickup (2 H inductance, 65 kΩ DC resistance) and the pedal's input impedance (250 kΩ). The pickup + cable capacitance (220 pF) forms a second-order RLC bandpass with a resonant peak near 3–5 kHz and a roll-off above it; design as a biquad peak/shelf using the Audio EQ Cookbook at the audio sample rate. At 1 Mω input impedance the resonance is pronounced (vintage tone stack sound); at 50 kΩ it is damped (buffered input). Expose `inputImpedance` (50 kΩ, 250 kΩ, 1 MΩ) as a three-way selector. Prototype in Python; generate golden WAVs for each impedance setting.

### 18e. Crossover distortion

- [ ] Add a `CrossoverDist` `DistortionType` that models a class-B transistor output stage with insufficient bias: apply a forward dead-band correction — if `0 < x < threshold`, push to `threshold`; if `−threshold < x < 0`, push to `−threshold`; otherwise pass through — where `threshold = 0.03 + 0.12 × drive`. The abrupt step at zero creates the "spitty", "fizzy" texture heard from a mis-biased transistor stage or an aging op-amp whose output stage is running out of quiescent current. Unlike `GatedFuzz` (16c), which zeros the dead-zone, this clips the dead-zone to the threshold level, creating a hard discontinuity rather than a silence. Prototype in Python; generate golden WAVs; add to `compare.py` with an appropriate per-mode tolerance.

### 18f. Sample rate reduction (LoFi aliasing)

- [ ] Add a `loFi` parameter (0–1, default 0 = full 48 kHz) that reduces the effective sample rate seen by the waveshaper by integer sample-holding: `decimFactor = max(1, round(1 / (1 − 0.98 × loFi)))` — so `loFi=0.5` ≈ 2× decimation (24 kHz effective rate) and `loFi=0.98` ≈ 50× decimation (≈ 960 Hz effective rate). Each input sample is held for `decimFactor` samples before being passed to the waveshaper, creating a staircase waveform that aliases intentionally inside the oversampled chain. Unlike `Bitcrush` (amplitude quantisation), this is temporal quantisation — it generates aliasing harmonics rather than quantisation noise. Apply within the 4× oversampled section so that oversampling does not undo the aliasing. Prototype in Python; generate golden WAVs.

### 18g. Optical compander

- [ ] Add a `compander` bool parameter that inserts a soft-knee opto-compressor before the drive stage and a matched expander after the output level scalar. The compressor uses an opto-cell model: `gainReduction_dB = max(0, (rms_dB − threshold) × (1 − 1/ratio))` with a logarithmic attack (`τ_atk = 20 ms`) and release (`τ_rel = 200 ms`) averaged on the RMS envelope; `threshold = −20 dBFS`, `ratio = 4:1`. The expander applies the exact inverse gain function so the overall input-to-output dynamic range is preserved — only the distribution across the drive stage changes, making the distortion respond more consistently across pick dynamics. Prototype in Python; generate golden WAVs for a wide-dynamic-range test signal.

## 19. Delay — Extended Modes

Creative and corrective delay modes that build on the quality foundation of section 14. Each mode is prototyped in Python, validated against C++ golden output, then exposed in both hosts.

### 19a. Freeze / looper

- [ ] Prototype in `python/delay.py` — add `--freeze` flag: latch the write pointer at the trigger point, force feedback to 1.0, mute dry input from the write path, and apply a 5 ms cross-fade ramp when freeze is released; generate a golden WAV that shows the loop sustaining after all input ceases.
- [ ] C++ — add `bool freeze` parameter to `Delay.h`; implement `frozenWritePos` (int) and `freezeGain` (`SmoothedValue`, 5 ms ramp) state; on `freeze` true→false edge, schedule a cross-fade from the frozen read pointer back to the live write pointer over 5 ms.
- [ ] Catch2 test — apply 500 ms of silence after freeze engages; confirm every output sample has `|value| > 0.01` (loop is sustaining) and no sample is `> 2.0` (no runaway).
- [ ] JUCE — add `dl_freeze` `AudioParameterBool` (default false); wire to `Delay::setFreeze()` in `processBlock()`; add a Freeze button to the Delay section of the editor.
- [ ] VCV Rack — add `FREEZE_PARAM` momentary toggle and `FREEZE_INPUT` gate jack (> 1 V engages); update `DelayModule.cpp` and `res/Delay.svg`.

### 19b. Comb filter / flanger mode

- [ ] Prototype in Python — add `--comb-mode` flag to `delay.py`: clamp time to 1–20 ms, set feedback to 0.85; generate golden WAVs for a static 5 ms comb and a swept 1–20 ms flanger (linear time sweep over 2 s).
- [ ] C++ — add `bool combMode` to `Delay.h`; when true, clamp `delaySamples` to `[sampleRate/1000, sampleRate/50]` regardless of the `time_ms` parameter; add `float combFeedback` (0.5–0.98) that overrides the global `feedback` when `combMode` is active.
- [ ] Catch2 test — in comb mode at time=5 ms, apply a swept sine input and confirm energy peaks at `f₀ = 200 Hz` and its harmonics; confirm no NaN/Inf at `combFeedback = 0.98`.
- [ ] JUCE — add `dl_comb_mode` `AudioParameterBool` and `dl_comb_feedback` `AudioParameterFloat` (0.5–0.98); wire both in `processBlock()`; expose in editor.
- [ ] VCV Rack — add `COMB_MODE_PARAM` toggle and `COMB_FB_PARAM` knob; update module and SVG.

### 19c. Stutter / beat repeat

- [ ] Prototype in Python — add `--stutter-length` and `--stutter-repeats` flags to `delay.py`; on a seeded trigger at t=1 s, capture a snapshot and play it back N times with 2 ms linear fade-in/out at each loop boundary; generate golden WAVs for lengths 50 ms and 250 ms.
- [ ] C++ — add `float stutterLengthMs` (16–500), `int stutterRepeats` (2–8), and `bool stutter` to `Delay.h`; implement `stutterBuf` (`std::vector<float>`, max 500 ms at 48 kHz = 24 000 samples), `stutterPos`, and `stutterCount` state machine; main write pointer continues advancing during playback.
- [ ] Catch2 test — trigger stutter at repeats=3, length=100 ms; count rising edges in the output and confirm exactly 3 playback cycles before release; confirm no sample `> 2.0`.
- [ ] JUCE — add `dl_stutter` bool, `dl_stutter_length` float, and `dl_stutter_repeats` int choice parameters; wire in `processBlock()`; add a Stutter button and the two secondary knobs to the editor.
- [ ] VCV Rack — add `STUTTER_PARAM` trigger, `STUTTER_INPUT` gate, `STUTTER_LENGTH_PARAM`, and `STUTTER_REPEATS_PARAM`; update module and SVG.

### 19d. Spectral / dispersive delay

- [ ] Prototype in Python — implement a cascade of eight 1st-order allpass sections (centre frequencies 100, 200, 400, 800, 1600, 3200, 6400, 12800 Hz) using `scipy.signal.lfilter`; generate golden WAVs for `dispersion=0` (must be flat / identical to no-dispersion path), `dispersion=0.5`, and `dispersion=1.0`.
- [ ] C++ — add `float dispersion` (0–1) to `Delay.h`; compute allpass coefficient `g = (tan(π×fc/fs) − 1) / (tan(π×fc/fs) + 1)` for each of the eight bands, scaled by `dispersion`; apply the bank to the wet signal after the delay read and before the mix blend; store eight biquad state pairs.
- [ ] Catch2 test — confirm `dispersion=0` produces identical output to the no-dispersion path (max error < 1e-6); confirm `dispersion=1` shifts the impulse centroid by at least 2 ms vs `dispersion=0`.
- [ ] JUCE — add `dl_dispersion` `AudioParameterFloat` (0–1, displayed as "%"); wire in `processBlock()`; add a "Disp" knob to the Delay editor secondary row.
- [ ] VCV Rack — add `DISPERSION_PARAM` knob and `DISPERSION_INPUT` CV jack; update module and SVG.

### 19e. Envelope-tracked feedback filter

- [ ] Prototype in Python — add `--env-filter` flag to `delay.py`: compute 10 ms RMS of the dry input, map to `fc = 800 × (6000/800)^rms_norm` Hz, update the 1-pole LP coefficient per sample; generate golden WAVs for a staccato sequence where cutoff modulation is audible in the repeats.
- [ ] C++ — replace the fixed `lpAlpha` in `Delay.h` with a per-sample computation driven by a short-term RMS accumulator (10 ms window); smooth the resulting `fc` with a 20 ms `SmoothedValue` to prevent coefficient chatter; add `bool envFilter` toggle to enable/disable the feature (off = original fixed 4 kHz behaviour).
- [ ] Catch2 test — process a loud burst (0 dBFS, 50 ms) followed by a quiet tail (−20 dBFS, 200 ms); confirm the LP cutoff is higher during the burst repeats than during the tail repeats by measuring the high-frequency energy ratio.
- [ ] JUCE — add `dl_env_filter` `AudioParameterBool` (default false); wire in `processBlock()`; add a small "Env" toggle to the Delay section.
- [ ] VCV Rack — add `ENV_FILTER_PARAM` toggle; update module and SVG.

### 19f. Polarity-flipped feedback

- [ ] Prototype in Python — add `--flip-polarity` flag to `delay.py`: track how many complete `delaySamples` revolutions the write pointer has completed and toggle `polaritySign` (±1) on each revolution; generate golden WAVs showing the comb notch at `f₀ = 1000/time_ms` Hz and peak at `f₀/2`.
- [ ] C++ — add `bool flipPolarity` and `int revolutionCounter` to `Delay.h`; increment counter each sample, reset and flip `polaritySign` when `revolutionCounter >= delaySamples`; apply sign in the feedback accumulation: `writeSample = in + feedback × lpState × polaritySign`.
- [ ] Catch2 test — with `flipPolarity=true` and `time_ms=10`, apply a broadband burst and measure spectral energy at 100 Hz (f₀) and 50 Hz (f₀/2); confirm the 50 Hz peak is at least 6 dB louder than 100 Hz.
- [ ] JUCE — add `dl_flip_polarity` `AudioParameterBool`; wire in `processBlock()`; add a "Flip" toggle to the Delay section.
- [ ] VCV Rack — add `FLIP_POLARITY_PARAM` toggle and `FLIP_POLARITY_INPUT` gate jack; update module and SVG.

### 19g. Chorus / doubler mode

- [ ] Prototype in Python — add `--chorus` flag to `delay.py`: clamp time to 10–30 ms, set feedback=0 and mix=0.5, modulate with a 0.3 Hz sine at ±5 ms depth; generate both mono (summed phases) and stereo (L = cos, R = sin) golden WAVs.
- [ ] C++ — add `bool chorus` to `Delay.h`; when active, clamp `delaySamples` to `[fs×0.01, fs×0.03]`, override `feedback` to 0, and modulate the read pointer with quadrature LFOs; provide a `setChorusPhase(float radians)` so the stereo processor (section 15) can offset the two channels by 90°.
- [ ] Catch2 test — with chorus enabled, confirm the output differs from a static-delay signal (LFO is active), feedback accumulation is zero, and no sample exceeds 2.0; confirm `setChorusPhase(π/2)` produces a different modulation waveform than `setChorusPhase(0)`.
- [ ] JUCE — add `dl_chorus` `AudioParameterBool`; wire in `processBlock()`, passing phase 0 to left and π/2 to right channel instances; add a "Chorus" toggle to the editor.
- [ ] VCV Rack — add `CHORUS_PARAM` toggle; set phase 0 on the module's single instance (mono); update module and SVG. Note: full stereo chorus requires section 15.

### 19h. Harmonic resonance tuning

- [ ] Prototype in Python — add `--resonant-pitch` (Hz) flag to `delay.py`: quantise the input pitch to the nearest 12-TET semitone from A440 (`f = 440 × 2^(round(12 × log2(p/440))/12)`), set `time_ms = 1000/f` and `feedback = 0.97`; generate golden WAVs for A2 (110 Hz) and E3 (165 Hz) showing sustained resonance.
- [ ] C++ — add `float resonantPitch` (0 = off, 55–880 Hz when active) to `Delay.h`; when non-zero, compute the 12-TET-quantised pitch and override `time_ms` and `feedback` before the normal parameter update path; guard against divide-by-zero if pitch rounds to zero.
- [ ] Catch2 test — set `resonantPitch=110`; apply a 110 Hz sine burst, then silence; confirm spectral energy at 110 Hz is still present 500 ms after input stops (resonator is sustaining) and no sample exceeds 2.0.
- [ ] JUCE — add `dl_resonant_pitch` `AudioParameterChoice` with a note-name list (off, A1 55 Hz … A5 880 Hz, 13 entries); wire in `processBlock()`.
- [ ] VCV Rack — add `RESONANT_PITCH_PARAM` knob (55–880 Hz) and `RESONANT_PITCH_INPUT` V/oct jack (maps via `f = 440 × 2^v`); update module and SVG.

## 20. VCV Rack Modules — GUI and Panel Improvements

The current modules are functional but have several gaps: the Overdrive SVG was not updated when 13d added Mid/Presence controls (those knobs and jacks have no panel labels), Pick Sensitivity has no UI in either host, the Delay module lacks trigger inputs expected in a Rack context, and neither module gives any visual feedback that it is processing audio.

### 20a. Fix Overdrive panel SVG for 13d additions

- [x] Update `res/Overdrive.svg` to add the missing labels introduced by the 13d update: knob labels "MID" and "PRES" positioned above the RoundSmallBlackKnob centres at mm2px(10.16, 76) and mm2px(30.48, 76); CV port labels "MID" and "PRS" below the MID_CV and PRESENCE_CV jack centres at mm2px(10.16, 107) and mm2px(30.48, 107); also fixed all pre-existing label y-positions which were misaligned with actual widget positions by ~50px.

### 20b. Add Pick Sensitivity to Overdrive module

- [x] `Overdrive::setPickSensitive(bool)` is implemented in the DSP library but `PICK_PARAM` is absent from `OverdriveModule::ParamIds`, there is no `PICK_CV_INPUT`, and `process()` never calls `setPickSensitive()` — the feature is completely inaccessible; add `PICK_PARAM` and `PICK_CV_INPUT` to their respective enums, call `configSwitch(PICK_PARAM, 0, 1, 1, "Pick Sensitivity", {"Off", "On"})` and `configInput(PICK_CV_INPUT, "Pick Sens CV")`, push the value to `effect.setPickSensitive(params[PICK_PARAM].getValue() > 0.5f)` in `process()`, add a `CKSS` toggle switch widget at mm2px(20.32, 85) and `PJ301MPort` CV jack at mm2px(20.32, 107), and add "PICK" / "PCK" labels to the SVG.

### 20c. Clip and signal indicator lights

- [x] Neither module provides any visual feedback that audio is flowing or that the signal is clipping; add `CLIP_LIGHT` and `SIGNAL_LIGHT` to each module's `LightIds`; in `OverdriveModule::process()`, drive `lights[CLIP_LIGHT].setSmoothBrightness()` red when the output magnitude exceeds 4.75 V (0.95 × 5 V), and `lights[SIGNAL_LIGHT].setSmoothBrightness()` green when it exceeds 0.05 V; add matching lights to `DelayModule`; add `createLightCentered<SmallLight<RedLight>>` and `<GreenLight>` widgets near each output jack in both widget constructors; add small circle markers at each light position in both SVGs.

### 20d. Distortion type and clip shape CV inputs for Overdrive

- [x] Currently the Type and Shape parameters can only be changed by turning a knob or using the right-click context menu — there is no way to modulate them from CV; add `TYPE_CV_INPUT` and `SHAPE_CV_INPUT` to `OverdriveModule::InputIds`; when connected, override the knob value with a snapped mapping: 0–8 V → type index `clamp(round(v * 0.5f), 0.f, 4.f)`, 0–4 V → shape index `clamp(round(v * 0.75f), 0.f, 2.f)`; add two `PJ301MPort` jacks to the widget and "TYPE" / "SHP" labels to the SVG; this makes the distortion character automatable from a sequencer, unlocking rhythmic distortion and patch-programmatic sound design. `TYPE_CV` at x=40.64, y=89 mm (fills reserved slot in CV row 2); `SHAPE_CV` at x=10.16, y=100 mm (left column between row 2 and audio section).

### 20e. Tap Tempo input on Delay module

- [x] A gate/trigger input for setting delay time from the interval between successive pulses is a standard Eurorack delay feature; add `TAP_INPUT` to `DelayModule::InputIds` and `configInput(TAP_INPUT, "Tap Tempo")`; in `process()`, detect rising edges of `inputs[TAP_INPUT]` (threshold 1.0 V, debounced with a `bool tapHigh` state variable); on each rising edge compute `tapIntervalMs = (args.sampleTime * tapSampleCount) * 1000.f` (clamped to 10–4000 ms) and call `effect.setTimeMs(tapIntervalMs, args.sampleRate)`; add a `PJ301MPort` labeled "TAP" to the widget and SVG near the TIME CV jack; a `tapPrimed` flag ensures the first tap only starts the counter — the second tap establishes the interval; tap-derived time overrides the TIME knob while the cable is connected; TIME CV still applies as an offset on the tap-derived base.

### 20f. Clock/BPM and V/oct pitch inputs on Delay module

- [x] Add two musical-timing inputs that are idiomatic in a Rack patch:
  - **CLK input** (`CLK_INPUT`): accepts a clock gate at BPM; measure the interval between rising edges in samples, convert to ms, and set delay time — same rising-edge detection as 20e but dedicated to tempo-locked patching when a master clock is present
  - **V/OCT input** (`PITCH_INPUT`): map 1 V/oct to delay time via `time_ms = 1000.f / (440.f * std::exp2(v))` — gives 2.27 ms at 0 V (440 Hz resonance), 4.53 ms at −1 V (220 Hz), etc.; prerequisite for the resonator mode in TODO 19h and for pitch-locked comb filtering in 19b
  - All three timing inputs share the same panel row at y=111 mm (TAP | CLK | V/OCT); priority is V/OCT > CLK > TAP > TIME knob; TIME CV offset is suppressed when V/OCT is connected (would detune the pitch-locked frequency)

### 20g. Widen Overdrive panel to 10HP

- [x] The current 8HP (40.64 mm) panel is too narrow to cleanly accommodate the 7 knobs (Drive, Tone, Level, Mid, Presence, Pick) and 8 CV jacks (Drive, Tone, Level, Mid, Presence, Pick, Type, Shape) that will exist after 20b and 20d; change the Overdrive SVG `viewBox` and `width` from 120 to 150 (10HP = 50.8 mm), reflow all SVG element x-positions proportionally, and update `OverdriveWidget` screw positions and all `mm2px` x-coordinates; reorganize the layout with Drive/Tone/Level as the primary column (centred at x = 25.4 mm), Mid/Presence/Pick in a second column of smaller knobs, and two rows of four CV jacks below; the Delay panel can stay at 8HP since it has fewer controls.

### 20h. Per-module bypass CV gate input

- [x] Add a `BYPASS_INPUT` gate jack to each module so individual effects can be bypassed from a gate sequencer or envelope; in `process()`, if `inputs[BYPASS_INPUT].isConnected()` and `inputs[BYPASS_INPUT].getVoltage() > 1.0f`, copy the audio input directly to the output and call `effect.reset()` (zeroing filter/delay history) — same semantics as `onReset()`; when the gate goes low, resume normal processing; add a `PJ301MPort` labeled "BYP" near each module's audio I/O section and update both SVGs.

### 20i. appendContextMenu for Delay module

- [x] `DelayModule` has no `appendContextMenu()` override — unlike the Overdrive module which exposes Type and Shape there, the Delay context menu is completely empty (just the default JUCE Rack items); add an override that exposes useful options: interpolation mode (None / Linear when TODO 14a lands), tape saturation toggle (when TODO 14d lands), and a "Reset delay buffer" action that calls `effect.reset()` without resetting the knob positions; the menu items should be stubs (greyed out) for features not yet implemented so users know they are planned.

### 20m. Ping-pong mode panel toggle for Delay module

- [ ] `pingPong` is only settable via the right-click context menu — there is no panel control or CV jack, making it inaccessible in a live performance or scripted patch; add a `PING_PONG_PARAM` CKSS toggle centered at x=25.4 mm on the row below the Self Osc toggle; add "P-P" label to `res/Delay.svg`; call `effect.setPingPong()` in `process()`.

### 20n. Expose Mod Depth and Diffusion CV inputs on Delay module

- [ ] Two secondary knobs on the Delay module (Mod Depth and Diffusion) have no corresponding CV jacks, making them static in a modular patch; add `MOD_DEPTH_INPUT` and `DIFFUSION_INPUT` to `DelayModule::InputIds`; map ±5 V to ±half the parameter range using the same convention as existing CV jacks; add `PJ301MPort` jacks labeled "MOD" and "DIFF" to the widget at appropriate panel positions and update `res/Delay.svg`.

### 20j. Consistent knob styles between modules

- [ ] Both modules currently mix `RoundBlackKnob` (large) and `RoundSmallBlackKnob` (small) with no visual connection to the panel accent colors; the Overdrive SVG uses gold `#c8a96e` and the Delay SVG uses blue `#6e9ac8` for all accent elements, but the stock black knobs carry neither color; create two thin custom knob SVGs (`res/GoldKnob.svg` / `res/BlueKnob.svg`) that use a dark body with the matching accent color on the pointer line and indicator dot, register them as `SvgKnob` subclasses in `plugin.hpp`, and swap them in for `RoundBlackKnob` in each widget constructor — this ties the panel color language to the interactive controls so the two modules read as visually distinct from each other.

### 20k. True bypass via `configBypass`

- [x] Neither `OverdriveModule` nor `DelayModule` calls `configBypass(AUDIO_INPUT, AUDIO_OUTPUT)` in their constructors; without this, right-click → Bypass in VCV Rack routes silence to the output rather than passing the audio signal through dry — completely muting the module instead of bypassing the effect; add `configBypass(AUDIO_INPUT, AUDIO_OUTPUT)` to both module constructors after the existing `configInput`/`configOutput` calls; this is idiomatic for any single-in/single-out effect module in VCV Rack 2.x and should be the default for every module added in future sections.

### 20l. Widen Delay panel to 10HP

- [x] The current 8HP (40.64 mm) panel is cramped with 9 knobs/toggles and 8 inputs spread across narrow columns; change the Delay SVG `viewBox` and `width` from 120 to 150 (10HP = 50.8 mm), update `DelayWidget` right-screw x from 29.14 mm to 49.3 mm, move the three large knobs (Time/Feedback/Mix) to the panel centre at x = 25.4 mm, spread the secondary control pairs (Mod/Diff, Sat/Age, Duck/Depth) to x = 10.16 mm and x = 40.64 mm so they flank the centred large knobs, and move the audio output jack and indicator lights to x = 40.64 mm; the CV row, timing row, audio-in, and bypass jacks remain at their existing x = 10.16/20.32/30.48 mm positions.

### 20o. Widen Overdrive panel to 16HP

- [ ] The 10HP panel is at capacity after 20d: CV row 2 fills all four slots (PRS/PCK/BAS/TYPE at y=89 mm) and SHAPE_CV occupies an isolated single-jack row at y=100 mm wedged between the CAB toggle (y=96 mm) and the audio row (y=112 mm). No clean position remains for SAG (16b), a cabinet type CV input (20r), future §16i extensions (input trim, boost, dirt cut), or the Gated/Octave-Fuzz DistortionType additions (16c/d) if they ever need panel knobs. Change the SVG `viewBox` and `width` from 150 to 240 (16HP = 81.28 mm); move right-screw x from 49.3 mm to 79.8 mm; reflow the two-column knob layout and CV rows across the wider panel, taking advantage of the extra horizontal space to spread controls more legibly; relocate SHAPE_CV from the isolated y=100 mm slot into the expanded CV row 2 so the y=100 mm area is freed; update `res/Overdrive.svg` and all `mm2px` x-coordinates in `OverdriveWidget`.

### 20p. Widen Delay panel to 16HP

- [ ] The 10HP Delay panel cannot cleanly accommodate pending features: the ping-pong toggle (20m) needs a row below Self Osc that doesn't exist; the MOD_DEPTH and DIFFUSION CV jacks (20n) need a 4th and 5th position in the CV row, but the only unused slot at x=40.64 mm, y=104 mm is only 5.5 mm above the SIGNAL_LIGHT at y=109.5 mm — less than the 10 mm VCV Rack minimum port spacing; adding a 3rd CV row would collide with the audio row at y=118 mm. Change the Delay SVG `viewBox` and `width` from 150 to 240 (16HP = 81.28 mm); move right-screw x to 79.8 mm; reflow the centred large-knob layout and secondary control pairs across the wider panel; expand the CV row to accommodate the new MOD and DIFF CV jacks from 20n; shift indicator lights to the new rightmost position above the audio output. Update `res/Delay.svg` and all `mm2px` x-coordinates in `DelayWidget`.

### 20q. Cabinet Type CV input for Overdrive

- [ ] TYPE_CV (20d) lets a sequencer change distortion character at CV rate; the cabinet type has the same need — switching between 1×12, 4×12, and combo IR presets from a gate/CV source is impossible without right-clicking mid-performance. Add `CABINET_TYPE_INPUT` to `OverdriveModule::InputIds`; map 0–4 V → type index `clamp(round(v * 0.5f), 0.f, 2.f)` (0 V = 1×12, ~2 V = 4×12, ~4 V = combo); when connected, override `CABINET_TYPE_PARAM`; label the jack "CAT" (cabinet type) to distinguish it from the CAB enable toggle; add the `PJ301MPort` at a position freed by the 16HP reflow in 20o; update `res/Overdrive.svg`. Prerequisite: 20o.

### 20r. CV inputs for Delay secondary parameters

- [ ] Only TIME, FEEDBACK, and MIX have CV jacks on the Delay panel; TAPE_AGE, DUCK_DEPTH, and SELF_OSC cannot be modulated from a Rack patch, which limits the module's usefulness in expressive patches (e.g., automating tape degradation via an LFO, or gating self-oscillation from a sequencer). Add `TAPE_AGE_INPUT` (±5 V → ±0.5 age offset, same ±half-range convention as other CV jacks), `DUCK_DEPTH_INPUT` (±5 V → ±0.5 depth offset), and `SELF_OSC_INPUT` (gate > 1 V enables self-oscillation, overriding the CKSS toggle) to `DelayModule::InputIds`; add `configInput()` calls and CV-aware logic in `process()`; add `PJ301MPort` widgets labeled "AGE", "DEP", and "OSC" at panel positions made available by the 16HP reflow in 20p; update `res/Delay.svg`. Prerequisite: 20p.

## 21. JUCE Plugin — GUI Improvements

The current editor is a functional prototype: fixed 600×300 window, default JUCE knob style, no visual feedback, and one DSP parameter (`od_pick_sens`) missing from the UI entirely. The items below address the gaps most likely to matter in real use — inside a DAW, with automation, across a recording session.

### 21a. Custom LookAndFeel

- [x] Create `juce-plugin/AnoLookAndFeel.h` subclassing `juce::LookAndFeel_V4`; override `drawRotarySlider()` to render a dark-gray track arc (270° sweep), a filled arc in section accent color (gold `0xffc8a96e` for Overdrive, blue `0xff4477aa` for Delay), and a bright pointer line — matching the hardware-pedal aesthetic of the VCV Rack SVG panels; override `drawComboBox()` for flat dark fill with an accent-colored border; install via `setLookAndFeel(&anoLnf)` in the editor constructor and remove in the destructor. 17 Catch2 tests in `juce-plugin/test_laf.cpp` cover color spec, per-component override propagation, LAF lifecycle, and rotary geometry; run with `ctest --test-dir build -R AnoLookAndFeel` (requires `-DBUILD_JUCE_PLUGIN=ON`).

### 21b. Resizable editor window

- [ ] Call `setResizable(true, true)` with `setResizeLimits(600, 260, 1200, 520)` in the `Distant-EchoEditor` constructor; update `resized()` to use proportional layout (fractions of `getWidth()` / `getHeight()`) rather than hardcoded `300`-pixel split; persist the user's last window size in APVTS as two hidden `AudioParameterInt` entries (`editor_w`, `editor_h`) so the size survives DAW project save/reload. This is the prerequisite for adding more knobs without crowding.

### 21c. Per-effect bypass buttons

- [x] Add `od_bypass` and `dl_bypass` `AudioParameterBool` parameters to `createParameterLayout()`; add a styled `juce::ToggleButton` (or a small LED-style `TextButton`) in each section header; in `processBlock()`, skip `overdrive[i].process()` and call `overdrive[i].reset()` when `od_bypass` is true, same for delay — this gives independent effect switching that the host's global bypass cannot provide and lets users A/B each effect mid-session.

### 21d. Signal clip and activity indicators

- [ ] Add a small colored indicator component in each section header: in the Overdrive section it turns red for 150 ms whenever any output sample exceeds 0.95 (clip warning), and dim green while signal is above 0.005; in the Delay section it pulses at the delay rate (derived from `dl_time`) using a repeating `juce::Timer`; implement via `std::atomic<bool>` flags written in `processBlock()` and read by a 30 Hz timer in the editor — the flags are the only cross-thread state; these provide immediate confirmation that the plugin is processing audio, catching "why is nothing happening" bugs quickly.

### 21e. Expose Pick Sensitivity in JUCE

- [x] Add `od_pick_sens` `AudioParameterBool` to `createParameterLayout()` — `Overdrive::setPickSensitive(bool)` already exists and the VCV Rack module already exposes it, but the JUCE APVTS and editor both omit it entirely; add a "Pick Sens" toggle button to the Overdrive section and call `overdrive[i].setPickSensitive(pickSens)` in `processBlock()`.

### 21f. Formatted value text for all parameters

- [x] Add `withStringFromValueFunction` lambdas to every float parameter that currently shows a raw `0.000`–`1.000` value: `od_drive` / `od_tone` / `od_level` / `dl_feedback` / `dl_mix` → `String(int(v*100)) + "%"`; `od_mid` → `(v >= 0 ? "+" : "") + String(v, 1) + " dB"`; `od_presence` → `"+" + String(v, 1) + " dB"`; `od_pick_sens` → `"On"/"Off"` — the `dl_time` "300 ms" pattern (added in section 11) is the model to follow for each one; also add matching `valueFromTextFunction` lambdas so typed values parse correctly.

### 21g. Tooltips

- [ ] Add a `juce::TooltipWindow tooltipWindow{this, 600}` member to `Distant-EchoEditor` and call `setTooltip()` on every slider, combo, and button with a one-sentence description: what the control does, its range, and any interaction notes (e.g., `"Drive — pre-amp gain into the waveshaper (0–100%). Higher values push harder into the clipping stage."`); this makes the plugin self-documenting for users who open it in a DAW without reading docs.

### 21h. Factory preset bank

- [ ] Add a `juce::ComboBox presetBox` spanning the full editor width above the two sections; populate it with 6–8 named presets covering common tones ("Clean Boost", "Crunch", "Plexi Roar", "Fuzz", "Slapback Echo", "Long Tape Echo", "Shimmer Pad"); store each as a `std::initializer_list<std::pair<const char*, float>>` of parameter-ID / normalized-value pairs; on `ComboBox::onChange`, iterate the list and call `apvts.getParameter(id)->setValueNotifyingHost(v)` — this routes through parameter smoothers so there are no clicks on recall; presets are factory-only (read-only) for now.

### 21i. Effect routing order toggle

- [ ] Add an `od_first` `AudioParameterBool` (default true = Overdrive → Delay) with a small swap-arrow icon button placed in the center divider between sections; in `processBlock()`, branch on `od_first` to run overdrive then delay, or delay then overdrive; "delay into drive" stacks repeats into the distortion (smears and compresses echoes) whereas "drive into delay" echoes the distorted signal cleanly — both are musically useful; this is one parameter and a two-branch if in the process loop.

### 21j. Section panel visual polish

- [ ] Draw a 1 px rounded-rect accent border around each section using its color (gold for OD, blue for Delay) in `paint()`; fill the interior with a subtly lighter dark (e.g., `0xff1e1e38`) than the background `0xff1a1a2e` to lift the sections visually; draw section labels with the accent color and a slightly larger font (15 pt bold); these color-coding choices match the VCV Rack panel SVG language — `Overdrive.svg` uses gold and `Delay.svg` uses blue — so the JUCE plugin and the Rack module feel like parts of the same family.

### 21k. Fix Delay section label color in PluginEditor

- [x] In `PluginEditor::paint()` (`juce-plugin/PluginEditor.cpp`), both "OVERDRIVE" and "DELAY" section headers are drawn with the same `juce::Colour(0xffc8a96e)` (gold) in a single `setColour` call; the DELAY header should use blue `0xff4477aa` to match the VCV Rack Delay panel accent color and the README description ("gold (Overdrive) and blue (Delay) header labels"); fix by calling `g.setColour(juce::Colour(0xffc8a96e))` before drawing "OVERDRIVE" then `g.setColour(juce::Colour(0xff4477aa))` before drawing "DELAY" — a two-line change; note that 21j's planned visual polish will also set these colors, so coordinate with that item when it lands.

### 21l. Override `drawToggleButton` in `AnoLookAndFeel`

- [ ] The current `AnoLookAndFeel` overrides `drawRotarySlider` and `drawComboBox` but not `drawToggleButton`, so all six toggle buttons in the editor (Bypass × 2, Pick Sensitivity, Cabinet IR, Tape Saturation, Self Oscillate, Ping-Pong) use the default JUCE V4 style — white or light-gray fill that clashes with the dark `0xff1a1a2e` background. Add `void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)` override to `AnoLookAndFeel`: draw a dark-fill rounded rectangle (`0xff222233`) with a 1 px border in the button's section accent color (the editor sets the accent on each button via `setColour(ToggleButton::tickColourId, accentColour)` after `setLookAndFeel`); draw a small filled circle ("LED") in the top-right corner in accent color when `getToggleState()` is true, or dimmed (`0xff444455`) when false; draw the button text in `0xffcccccc`; add three Catch2 tests to `juce-plugin/test_laf.cpp`: tick color propagates via `findColour`, unchecked LED color is dim, checked LED color matches the accent.

### 21m. Proportional font scaling when resizable editor (21b) lands

- [ ] The current editor uses hardcoded font sizes in two places: `setupKnob()` in `PluginEditor.cpp` sets 12pt for knob labels, and `PluginEditor::paint()` uses 13pt bold for section headers and 9pt for "ANOESIS" subtitles. When TODO 21b makes the editor resizable (min 600×260, max 1200×520), these sizes will look wrong at non-default sizes. Fix: replace the hardcoded values with computed sizes derived from `getHeight()` — e.g., `headerFont = std::max(10.0f, getHeight() * 0.0325f)` (≈13pt at 400 px) and `labelFont = std::max(8.0f, getHeight() * 0.03f)` (≈12pt at 400 px); call `repaint()` from `resized()` so the header text redraws. Do this as part of the 21b implementation rather than separately. Also update `drawRotarySlider` in `AnoLookAndFeel` to accept an externally-set text box height rather than the fixed 18 px currently passed through `setTextBoxStyle`.

## 22. Documentation Improvements

These items make `docs/` more complete, consistent, and useful to readers coming from outside the project.

### 22a. Expand `docs/Design.md` "Adding a New Effect" to include JUCE and VCV Rack integration

- [x] The eight-step guide currently ends at C++ and `docs/` — it says nothing about wiring a new effect into either plugin host; add step 9 for JUCE: register `AudioParameterFloat`/`AudioParameterBool` entries in `createParameterLayout()`, push values to the effect in `processBlock()`, and add the corresponding slider/knob to `PluginEditor`; add step 10 for VCV Rack: create a new `Module` subclass, add `configParam`/`configInput` entries for each knob and CV, call the effect's setter methods in `process()`, add the widget to the plugin registration in `plugin.cpp`, and design the panel SVG — currently a contributor following this guide hits a wall after implementing the C++ header.

### 22b. Add Catch2 test cross-reference to `docs/Delay.md`

- [x] `docs/Overdrive.md` ends with a "File Location" block that lists the Python prototype, golden WAVs, and `tests/test_overdrive.cpp`; `docs/Delay.md` has the same block but omits the Catch2 test file — add `Catch2 tests: tests/test_delay.cpp` so readers following the Overdrive pattern know where to find the Delay tests.

### 22c. Clarify `SmoothedValue::reset()` snap behavior in `docs/SmoothedValue.md`

- [x] The API table lists `s.reset(value)` as "snap to value immediately (no ramp)" but does not state that it sets *both* `current` and `target` to the given value — meaning `isSmoothing()` returns `false` immediately after and `next()` returns `value` on every subsequent call until `setTarget()` is called again; this matters for code that calls `reset()` before audio starts (correct) versus code that calls `reset()` mid-stream expecting it to resume smoothing from the snapped position (incorrect).

### 22d. Document `HardClip` small-signal gain in `docs/Overdrive.md`

- [x] The Chebyshev polynomial `(3x − x³) / 2` has a slope of 1.5 at x = 0, giving a 3.6 dB gain increase for small signals relative to the unity-gain path; this means `HardClip` mode is 3.6 dB louder than `SoftClip` or `Asymmetric` for signals well below the clip threshold; document this in the HardClip waveshaper section so users calibrating level differences across modes understand why HardClip sounds louder at low drive settings.

### 22e. Clarify biquad redesign rate for `mid` and `presence` in `docs/Overdrive.md` and `docs/Design.md`

- [x] Both documents say biquad redesign for `mid`/`presence` happens "once per `process()` call when the value has changed" but neither explains that the coefficients are held constant for the entire block (not updated per-sample like `SmoothedValue`), and neither explains the condition "has changed" — in practice this means the smoothed value differs from what it was at the last `process()` call; add a sentence clarifying this block-rate redesign policy and noting that it trades per-sample control precision for 1 biquad-design per block instead of SR/blockSize designs per second.

### 22f. Add a "Current Status / Known Limitations" section to `README.md`

- [x] The SVG panel label gaps, missing pick sensitivity UI in JUCE, integer-only delay time, and abrupt time-change click are currently described inline within the JUCE Plugin Editor and VCV Rack Modules sections; aggregate them into a short table at a visible location near the top of the README so users building from source know upfront what is and is not finished — reduces confusion when a user opens the JUCE editor and sees no "Pick" knob or loads the Overdrive module and sees unlabelled controls.

### 22g. Fix or remove inaccurate `od_pick_sens` description in `docs/Overdrive.md`

- [x] The Overdrive parameters table lists `pickSens` type as `bool` (correct for the C++ API — `setPickSensitive(bool)`) but the table row for the JUCE plugin in `README.md` and references in TODO items describe it as a 0–1 float percent; the Python prototype passes it as a flag; confirm that the VCV Rack parameter type when implemented should be `configSwitch` (not `configParam`) and update all cross-references in README, CLAUDE.md, and docs/ to consistently describe it as a boolean on/off toggle, not a continuous parameter.

### 22h. Correct stale "state reset" text in `docs/Overdrive.md`

- [x] The Filter Coefficient Lifecycle table in `docs/Overdrive.md` correctly states "(no state reset)" for the mid/presence biquad redesign, but the explanatory paragraph immediately below the table still says "so their biquads are recomputed (and state reset) at the start of the next `process()` call. The state reset avoids transients from the coefficient discontinuity." — both sentences became false after section 11 replaced the state-reset approach with SmoothedValue-driven coefficient updates that deliberately avoid flushing filter state; update the paragraph to describe the actual behavior: biquads are redesigned at block rate without a state reset, and the filter settles naturally from incremental coefficient shifts — matching what the table already says and what the code does.

### 22i. Add docs pages for new utility classes

- [ ] Write `docs/BiquadFilter.md` documenting the `BiquadFilter` class: the `Coeffs` struct layout, `process()` / `reset()` / `setCoeffs()` API, and each static design function (`designHighShelf`, `designButterworthLP`, `designButterworthHP`, `designPeaking`) with the Audio EQ Cookbook reference, expected gain at the corner frequency, and a worked example. Include a note that the high-shelf and peaking designs match the filter math already used in `Overdrive.h` before extraction.
- [ ] Write `docs/EnvelopeFollower.md` documenting the `EnvelopeFollower` class: the asymmetric attack/release IIR formula, the `prepare(sampleRate, attackMs, releaseMs)` API, how the coefficient `exp(-1/(sr·τ))` maps to settling time, and the two canonical uses in this project (pick sensitivity at 1 ms / 100 ms in `Overdrive`; ducking at 5 ms / 500 ms in `Delay`).
- [ ] Write `docs/OnePoleFilter.md` documenting `OnePoleLP`: the difference equation, `setCutoff(fs, fc)` vs `setAlpha(a)` API, and the note that `alpha = 1 - exp(-2π·fc/fs)` produces a −3 dB point near `fc` (see lessons-learned for discrete-time warping detail).
- [ ] Write `docs/AllpassFilter.md` documenting `AllpassFilter`: the Schroeder transfer function, the single-delay-line implementation (`v[n] = x[n] + g·v[n-D]`, `y[n] = v[n-D] - g·v[n]`), the double-precision internal state, a warning that for large D the settling time is O(D²/ln(2)) samples, and a caution that the output formula must use `v[n]` (not `x[n]`) — substituting `x[n]` gives DC gain `(1+g²)/(1−g)` per stage; see `docs/lessons-learned.md` §14f for the full post-mortem.
- [ ] Update `docs/Overdrive.md` and `docs/Delay.md` to replace inline descriptions of biquad, envelope-follower, and one-pole LP math with links/references to the new doc pages.

### 22j. Add `BiquadFilter::designLowShelf` and `designBandpass` for future effects

- [ ] Reverb (§23), tremolo harmonic crossover (§24b), and the compressor side-chain (§25) will need low-shelf and band-pass biquad designs that follow the same Audio EQ Cookbook formulas; add them to `BiquadFilter.h` as static methods now so future effects can use the shared class rather than embedding their own filter design code; add corresponding Catch2 tests to `tests/test_biquad.cpp` verifying unity gain at DC (low-shelf) and correct boost at the centre frequency (band-pass); keep them untouched by `Overdrive.h` and `Delay.h` until actually used.

### 22k. Fix IDE IntelliSense path for Catch2 headers

- [ ] The VS Code C/C++ extension shows false-positive errors on all `tests/*.cpp` files because `catch2/catch_test_macros.hpp` is not in the extension's include path; the compiler finds Catch2 via CMake's `target_link_libraries(effects_tests Catch2::Catch2WithMain)`, but the extension needs a separate `c_cpp_properties.json` entry; run `cmake --build build` with the `CMAKE_EXPORT_COMPILE_COMMANDS=ON` flag and point the extension at `build/compile_commands.json` via `"compileCommands": "${workspaceFolder}/build/compile_commands.json"` in `.vscode/c_cpp_properties.json`; verify that the Catch2 squiggles disappear in the IDE after the configuration change.

### 22m. Update docs/Overdrive.md for CabinetType and multiple cabinet presets

- [x] Added "Speaker Cabinet IR" section to `docs/Overdrive.md` listing all three presets with design targets (1×12: 120 Hz resonance, −6 dB/oct above 4 kHz; 4×12: 80 Hz resonance, −3 dB/oct above 3 kHz; combo: 180 Hz resonance, −6 dB/oct above 5 kHz), documenting `od_cabinet_type` (choice: 0/1/2), noting `CabinetIR::prepare()` resamples to session sample rate, and linking to `CabinetIR.md`.

### 22n. Add docs/CabinetIR.md

- [x] Rewrote `docs/CabinetIR.md` to cover all three presets and the new API: `CabinetType` enum values, `setEnabled`/`setType`/`getType`, `buildActiveIR()` sample-rate resampling (linear interpolation from 48 kHz base IR), circular-buffer direct-form convolution, all three data headers and `--generate-header --type` regeneration commands, JUCE (`od_cabinet`/`od_cabinet_type`) and VCV Rack (`CABINET_PARAM`/`CABINET_TYPE_PARAM`) integration.

### 22l. Add `AllpassFilter` DC steady-state Catch2 test

- [ ] The §14f post-mortem identifies a quick sanity check that would have caught the wrong output formula before the golden-WAV pipeline was needed: drive `AllpassFilter` with a constant DC input and verify the steady-state output equals the input (an all-pass filter has gain = 1 at all frequencies, including DC); add to `tests/test_delay.cpp`: instantiate `AllpassFilter` with `D=8, g=0.5` (settling time ≈ 424 samples per the settling-time lesson); push `1.0f` for 2000 samples; confirm the final output is within `1e-3` of `1.0f`; also confirm a constant `0.0f` input produces `0.0f` output. With the wrong output formula, DC gain per stage is `(1 + g²) / (1 − g) = 1.5` — four stages in series produce `1.5⁴ ≈ 5.06`, which this test would reject immediately.

### 22p. Document `laf_tests` CMake target and CTest filter pattern

- [x] Updated `CLAUDE.md`, `README.md`, and `docs/Design.md`: CTest `-R` filter patterns documented with examples for all suites (Overdrive, CabinetIR, Delay, AnoLookAndFeel); `docs/Design.md` adds a dedicated "CTest filter patterns" subsection explaining the `catch_discover_tests` title-matching rule and the `laf_tests` `juce_add_console_app` + `ScopedJuceInitialiser_GUI` pattern for JUCE-side Catch2 tests.

### 22q. Update CLAUDE.md for cabinet test filter, `wav_compare --type`, and `test_cabinet.cpp`

- [x] Updated `CLAUDE.md` C++ Testing section: added `ctest -R CabinetIR` example with parenthetical "not `-R test_cabinet`"; added general rule that `-R` matches test-case title prefix, not file name; added `wav_compare cabinet in.wav out.wav --type 4x12|combo` to the `wav_compare` description. Also fixed `-R test_overdrive` → `-R Overdrive` in `README.md` and added `CabinetIR` example there.

### 22r. Add cabinet module pytest tests to `python/tests/test_effects.py`

- [ ] `python/tests/test_effects.py` tests the overdrive and delay Python prototypes (silence-in/silence-out, NaN/Inf bounds, asymmetric clipping, impulse timing, feedback decay) but has no coverage for `cabinet_ir.py`; add pytest cases: (1) `process()` of all-zero input returns all-zero output for each cabinet type; (2) no NaN/Inf when processing a full-scale ±1.0 signal through each type; (3) the 1 kHz steady-state RMS is within 10% of the input RMS (IR normalised at 1 kHz) for each type, measured past the 256-tap startup transient; (4) the output for the 4x12 type at 10 kHz has higher RMS than the 1x12 type (verifying the shallower −3 dB/oct rolloff is present in the Python prototype as well as in C++); these mirror the C++ Catch2 tests added in `tests/test_cabinet.cpp` and ensure the Python golden source is also correct.

## 26. Component Library Enhancements

These items improve `libs/effects/` utility classes based on patterns observed during the §22 refactoring. They are independent of specific effects and can be done in any order.

### 26a. `BiquadFilter` — add `designNotch` for spectral noise cancellation

- [ ] A notch filter (infinite null at fc, Q controls width) is useful for hum removal and feedback cancellation; add `static Coeffs designNotch(double fs, double fc, double Q)` using the Audio EQ Cookbook formula (`b0 = b2 = 1`, `b1 = a1 = -2cos(ω0)`, `a0 = 1+α`, `a2 = 1-α`, `α = sin(ω0)/(2Q)`); add a Catch2 test verifying that the output power at `fc` is at least 40 dB below a test sine at `fc/2`.

### 26b. `OnePoleLP` — add a companion `OnePoleHP` class

- [ ] The DC-block HP in `Overdrive.h` (at the oversampled rate) is still implemented as raw float coefficients; extract it into an `OnePoleHP` class in `OnePoleFilter.h` using the bilinear-derived formula already in `prepare()`; expose `setCutoff(double fs, double fc)` and the same `process()` / `reset()` / `get()` API as `OnePoleLP`; refactor the `Overdrive.h` DC block to use `OnePoleHP`; add Catch2 tests for DC rejection and −3 dB point.

### 26c. `SmoothedValue` — add `prepareNoReset()` for mid-stream rate changes

- [ ] `SmoothedValue::prepare()` currently snaps `current = target` on every call, making it impossible to call `prepare()` when the host's sample rate changes mid-session without causing an audible discontinuity; add `prepareNoReset(double sampleRate, double rampMs)` that recomputes `coeff` but leaves `current` and `target` unchanged — the smoother continues from its current state at the new rate; add a Catch2 test confirming that `prepareNoReset()` does not change `current` or `isSmoothing()` status.

### 26d. `AllpassFilter` — expose coefficient `g` as a run-time parameter

- [ ] The current `AllpassFilter::prepare(int D, float g)` bakes `g` in at allocation time; the reverb in §23 will need to modulate `g` for decay control (changing `g` scales all FDN feedback coefficients); add `setCoeff(float g)` that updates the internal `this->g` without touching the delay line — changing `g` mid-stream will produce a smooth transition because the existing state naturally settles to the new steady-state response; add a Catch2 test confirming that the all-pass unity-magnitude property holds before and after a `setCoeff()` change (with adequate settling time).

### 26e. Add `include/` public-header umbrella for the effects library

- [ ] Consumers of `libs/effects/` currently include individual headers by name; a single umbrella header `libs/effects/effects.h` that `#include`s all effect and utility headers would simplify integration in new host targets (a future web-audio or command-line tool); create the umbrella, update `libs/effects/CMakeLists.txt` to install it alongside the `INTERFACE` target, and verify that both `juce-plugin/` and `vcv-rack/` can replace their individual includes with the umbrella without compile errors.

Spring and plate reverb are the dominant reverb types in guitar contexts — spring reverb is built into most guitar amplifiers, and plate reverb defines the studio sound of classic rock recordings. Both use circuit-emulation targets (coil physics for spring, metal-plate resonance for plate) rather than a generic algorithmic reverb. These items follow the same Python-first, golden-WAV workflow as the rest of the project.

### 23a. Spring reverb — Python prototype and C++ implementation

- [ ] Add `python/reverb.py` implementing a spring reverb model: four Schroeder allpass stages in series (delays 5, 17, 29, 43 ms; coefficients 0.5) feed a two-line feedback delay network (FDN) with delay lengths 49 ms and 73 ms cross-coupled by a 2×2 Hadamard matrix; apply a single-pole LP (fc = 6 kHz) to each FDN output to model spring-coil HF absorption; add a spring ripple notch (2nd-order IIR notch at 2 kHz, Q = 8) to the wet output — this characteristic boing is the primary perceptual cue of a spring reverb; parameters: `decay` (0.1–0.9, scales FDN feedback coefficients), `mix` (0–1 wet/dry), `predelay_ms` (0–50 ms circular buffer before allpass input); validate silence-in/silence-out and that energy decays to −60 dBFS within `decay × 3 + 0.5` seconds; generate golden WAVs for `decay=0.4`, `decay=0.7`, `mix=0.5`; implement `libs/effects/Reverb.h` extending `Effect`; confirm C++ within 5e-4; add `Reverb` to `tests/wav_compare.cpp` and `python/compare.py`; write `tests/test_reverb.cpp` (silence-in, no NaN/Inf at extreme decay, energy monotonically decays after impulse); add `docs/Reverb.md`.

### 23b. Plate reverb mode

- [ ] Add a `ReverbType` enum (`Spring`, `Plate`) to `Reverb.h`; the `Plate` mode uses a six-line FDN (delays 29, 37, 43, 53, 61, 67 ms; 6×6 Hadamard matrix) and a 2nd-order LP (fc = 9 kHz) in each feedback line — denser, smoother tail without spring colouration; remove the ripple notch in plate mode; prototype in `python/reverb.py --mode plate`; generate golden WAVs; confirm C++ within 5e-4; expose as a `rv_type` choice parameter in JUCE (`AudioProcessorValueTreeState`) and via right-click context menu in VCV Rack.

### 23c. Decay, size, and damping controls

- [ ] Expose three continuously-varying parameters in both hosts: `rv_decay` (0.1–0.9, `SmoothedValue` 20 ms ramp — scales FDN feedback coefficients per block), `rv_size` (0.5–2.0, multiplies all FDN delay lengths and allpass delays at `prepare()` time, requiring buffer reallocation), `rv_damping` (0–1, maps to LP cutoff 8 kHz → 1.5 kHz); add all three as `AudioParameterFloat` in `createParameterLayout()` and as VCV Rack knobs with CV jacks; prototype parameter sweeps in Python and generate golden WAVs confirming expected decay-time and bandwidth behaviour; note that changing `rv_size` mid-session calls `prepare()` again — document this in `docs/Reverb.md` and add a Catch2 test confirming no clicks occur when size changes between blocks.

### 23d. Pre-delay and stereo output

- [ ] Add `rv_predelay` (0–100 ms, `AudioParameterFloat`) implemented as a short circular buffer inserted before the allpass input — separates the direct sound from the reverb onset without affecting the dry path; add `processStereo()` (prerequisite: section 15) that reads from two decorrelated FDN output lines (tapped at different FDN delay positions) for a wide stereo field; expose `rv_predelay` in both hosts; generate golden WAVs for `predelay_ms=20` and `predelay_ms=50`; add `docs/Reverb.md` section on stereo operation and cite section 15 as a prerequisite.

## 24. Tremolo

Tremolo is periodic amplitude modulation of the output signal. Guitar amplifier tremolo circuits of the 1950s–60s used LDR-based optical cells and tube-bias modulation, each producing a characteristic waveform shape. The harmonic tremolo (used in mid-period Fender amps) splits the signal into two bands and modulates them out of phase, producing a richer tonal movement than simple gain modulation. These items target circuit-emulation fidelity for both types.

### 24a. Core optical tremolo — Python prototype and C++ implementation

- [ ] Add `python/tremolo.py` implementing an optical tremolo: a sine LFO (`rate` 0.5–10 Hz, `depth` 0–1) modulates a gain multiplier — `gain[n] = 1 − depth × 0.5 × (1 − cos(2π × lfoPhase[n]))`, giving gain range `[1−depth, 1]` at full depth with 0 dB at depth=0; LFO phase accumulates `2π × rate / sampleRate` per sample and wraps at 2π; add a `wave` parameter (Sine / Triangle / Square) where Triangle uses `abs(lfoPhase/π − 1) × 2 − 1` and Square uses `sign(sin(2π × lfoPhase))` with a 1 ms crossfade at transitions to prevent clicks; validate silence-in/silence-out and no NaN/Inf at extreme rate and depth; generate golden WAVs for `rate=4, depth=0.8, wave=Sine` and `rate=6, depth=1.0, wave=Square`; implement `libs/effects/Tremolo.h` extending `Effect`; confirm C++ within 5e-4; add to `tests/wav_compare.cpp` and `python/compare.py`; write `tests/test_tremolo.cpp` (silence-in, LFO period accuracy: peak-to-peak interval within 1 sample of `sampleRate/rate`, no NaN/Inf at extreme parameters); add `docs/Tremolo.md`.

### 24b. Harmonic tremolo mode

- [ ] Add `TremoloType` enum (`Optical`, `Harmonic`) to `Tremolo.h`; in `Harmonic` mode, split the signal with a 1 kHz 2nd-order Linkwitz-Riley crossover (LP + HP summing to all-pass), apply the LFO gain to the LP band and the inverse gain `(1 − gain[n])` to the HP band, then sum — bass and treble duck alternately rather than the full signal ducking together, producing the characteristic "wobble" of mid-period Fender amps; prototype in `python/tremolo.py --mode harmonic`; generate golden WAVs; confirm C++ within 5e-4; expose as a `tr_type` choice parameter in JUCE and via right-click context menu in VCV Rack; add a Catch2 test confirming that in Harmonic mode the LP and HP output signals are 180° out of phase in their amplitude envelopes.

### 24c. JUCE and VCV Rack integration

- [ ] Add `Tremolo` to the JUCE `AudioProcessor`: expose `tr_rate` (0.5–10 Hz, skewed), `tr_depth` (0–1), `tr_wave` (0–2 choice: Sine/Triangle/Square), `tr_type` (0–1 choice: Optical/Harmonic), and `tr_bypass` (`AudioParameterBool`) in `createParameterLayout()`; add a Tremolo section to `PluginEditor` with Rate and Depth knobs, Wave and Type combo boxes, and a bypass toggle; use accent colour teal `0xff44aa88` to distinguish it from the Overdrive (gold) and Delay (blue) sections; add `TremoloModule` in `vcv-rack/src/TremoloModule.cpp` with Rate/Depth knobs and CV jacks on all parameters, `BYPASS_INPUT` gate jack, and a `res/Tremolo.svg` panel (8HP, teal accent `#44aa88`); register in `plugin.cpp`; update `docs/Tremolo.md` with the full API.

## 25. Compressor

The Project Goal lists "compressor, noise gate, optical compander" as distinct dynamics targets. The noise gate (TODO 16f) and optical compander (TODO 18g) are integrated into the Overdrive signal chain. This section adds a standalone `Compressor` effect — a general-purpose soft-knee RMS compressor with a limiter mode — that operates as its own JUCE plugin section and VCV Rack module, usable independently of the Overdrive.

### 25a. Core RMS compressor — Python prototype and C++ implementation

- [ ] Add `python/compressor.py` implementing a soft-knee RMS compressor: compute short-term RMS over a 10 ms sliding window (`windowSamples = round(0.01 × sampleRate)`); apply gain reduction `GR_dB = max(0, (rms_dB − threshold + knee/2)² / (2 × knee))` within the knee region and `GR_dB = (rms_dB − threshold) × (1 − 1/ratio)` above it (standard soft-knee formula); smooth through ballistics (`attack_ms`, `release_ms`) using `τ = exp(−1 / (τ_ms × sampleRate / 1000))`; apply `makeupGain` (dB) after gain reduction; parameters: `threshold` (−60 to 0 dBFS), `ratio` (1:1 to 20:1), `attack_ms` (0.1–100 ms), `release_ms` (5–2000 ms), `makeupGain` (0–+24 dB), `knee` (0–12 dB soft-knee width); validate silence-in/silence-out and that a signal 6 dB above threshold with ratio=4 is attenuated by 4.5 dB; generate golden WAVs for `threshold=−20, ratio=4, attack=10, release=200` on a loud/quiet alternating test signal; implement `libs/effects/Compressor.h` extending `Effect`; add to `tests/wav_compare.cpp` and `python/compare.py`; write `tests/test_compressor.cpp` (silence-in, gain-reduction ratio accuracy ±0.5 dB at steady state, no NaN/Inf at extreme parameters, no output above threshold + 1 dB with ratio=20); add `docs/Compressor.md`.

### 25b. JUCE and VCV Rack integration

- [ ] Add `Compressor` to the JUCE plugin: `cp_threshold`, `cp_ratio`, `cp_attack`, `cp_release`, `cp_makeup` `AudioParameterFloat` and `cp_bypass` `AudioParameterBool` in `createParameterLayout()`; add a Compressor section to `PluginEditor` with Threshold, Ratio, Attack, Release, and Makeup knobs, accent colour amber `0xffaa8844`; add a gain-reduction meter — a vertical `juce::Component` bar (0–20 dB scale) reading `std::atomic<float> grDb` written in `processBlock()` and repainted by a 30 Hz timer — so compression depth is visible in the UI; add `CompressorModule` in `vcv-rack/src/CompressorModule.cpp` with the same five parameter knobs and CV jacks, a side-chain audio input port (`SC_INPUT`) that replaces the main input in gain-reduction computation when connected, and a GR CV output jack (0 V = 0 dB GR, 10 V = 20 dB GR) for external metering or side-chain ducking; `res/Compressor.svg` 8HP panel with amber accent `#aa8844`; register in `plugin.cpp`.

### 25c. Look-ahead and limiter mode

- [ ] Add `cp_lookahead` (0–10 ms `AudioParameterFloat`, default 0) implemented as a circular delay on the main audio path while gain computation uses the undelayed signal — allows the compressor to react before the transient arrives, eliminating the brief overshoot at fast attack times; implement as a `std::vector<float>` circular buffer of `ceil(lookahead_ms × sampleRate / 1000)` samples in `Compressor::process()`; add `cp_limit` `AudioParameterBool` that forces `ratio = ∞` (implemented as ratio=1000), `attack_ms = 0.1`, and `knee = 0`, turning the compressor into a brick-wall limiter; prototype both in Python and generate golden WAVs confirming that `limitMode=true` with `threshold=−3 dBFS` produces no output sample exceeding −3 dBFS after the attack phase; expose both in JUCE and VCV Rack; add a Catch2 test for the limiter guarantee.
