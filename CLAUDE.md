# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This project emulates guitar effects. The DSP algorithms are shared across two plugin targets — a JUCE audio plugin and a VCV Rack module — and are prototyped first in Python before being implemented in C++.

## Directory Structure

```text
/
├── libs/
│   ├── effects/    # shared C++ DSP library (no JUCE or VCV Rack headers allowed)
│   └── juce/       # JUCE git submodule
├── juce-plugin/    # JUCE AudioProcessor + editor UI
├── vcv-rack/       # VCV Rack module(s)
├── python/         # Python prototypes and golden-output validation scripts
├── tests/          # Catch2 unit tests for the effects library
└── docs/           # Markdown documentation and DSP plots for libs/effects
```

## Build System

CMake + Ninja. All targets build from the root:

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure          # run C++ tests

# JUCE plugin (requires juce submodule to be initialized)
cmake -B build -G Ninja -DBUILD_JUCE_PLUGIN=ON
cmake --build build --target Distant-Echo_Standalone  # or Distant-Echo_VST3
```

Install prerequisites via Homebrew:

```sh
brew install cmake ninja catch2 uv ruff zstd
brew install --cask pluginval   # JUCE VST3/AU validation (optional)
```

`auval` ships with Xcode Command Line Tools — no separate install needed.

JUCE lives in the `juce/` git submodule. Initialize it after cloning:

```sh
git submodule update --init libs/juce
```

Override `JUCE_DIR` to point at a different local clone if needed.

The VCV Rack SDK is **not** fetched automatically. Download it from vcvrack.com and unzip to `~/Rack-SDK` (the CMake default). Override with `-DRACK_SDK_DIR=...` if needed. Additional targets:

```sh
cmake -B build -G Ninja   # ~/Rack-SDK used by default
cmake --build build --target Distant-EchoRack           # build plugin.dylib
cmake --build build --target install-rack-plugin      # install to ~/Library/.../Rack2/plugins-mac-arm64
cmake --build build --target dist-rack-plugin         # produce .vcvplugin archive (requires zstd)
```

JUCE install targets (macOS user directories):

```sh
cmake --build build --target install-vst3        # → ~/Library/Audio/Plug-Ins/VST3/
cmake --build build --target install-au          # → ~/Library/Audio/Plug-Ins/Components/
cmake --build build --target install-standalone  # → ~/Applications/
cmake --build build --target install-juce        # all three
```

Plugin validation (requires JUCE build):

```sh
# VST3 — pluginval strictness level 5
pluginval --strictness-level 5 --validate-in-process \
    build/juce-plugin/Distant-Echo_artefacts/VST3/Distant-Echo.vst3

# AU — Apple's auval (bundled with Xcode CLT)
auval -v aufx Anoe Anoe
```

## Python Environment

Managed with `uv` (installed via `brew install uv`). The Python project lives in `python/`.

```sh
uv run --project python python/overdrive.py in.wav out.wav --drive 0.7 --mode softclip --plot
uv run --project python python/delay.py in.wav out.wav --time-ms 300 --feedback 0.4
uv run --project python python/compare.py --all           # diff golden vs C++ output
uv run --project python python/generate_docs_plots.py    # regenerate docs/img/ PNG plots
uv add --project python <package>                         # add a dependency
ruff check python/                                        # lint (ruff installed via brew)
uv run --project python pytest                            # run Python unit tests
```

**Golden WAV validation** (run after any algorithm change):

```sh
make golden        # (re)generate tests/golden/input/*.wav + tests/golden/*.wav via Python
make validate      # build wav_compare, run C++ on all golden inputs → tests/output/
make compare       # validate + diff every golden WAV against its C++ output (exits non-zero on failure)
make clean-output  # remove tests/output/
```

Key modules:

| File | Purpose |
| ------ | --------- |
| `utils.py` | Shared helpers: `load_wav`, `save_wav`, `process_blocks`, `sine`, `silence`, `compare_outputs`, `plot_*`, `DelayLine` |
| `overdrive.py` | Overdrive prototype: drive, tone, level, type (HardClip/SoftClip/Foldback/Asymmetric/Bitcrush), shape (Flat/MidFocus/BrightFocus), mid, presence, pickSens, bias |
| `cabinet_ir.py` | Cabinet IR designer; three presets via `--type {1x12,4x12,combo}` (1×12 open-back: 120 Hz resonance, −6 dB/oct above 4 kHz; 4×12 closed-back: 80 Hz resonance, −3 dB/oct above 3 kHz; 1×12 combo: 180 Hz resonance, −6 dB/oct above 5 kHz); `--generate-header` writes the matching data header; `--validate` checks direct-form vs fftconvolve (max err ≈ 9e-17); `process()` is the golden-WAV reference (clips output to ±1.0) |
| `delay.py` | Feedback delay prototype (time\_ms, feedback, mix, interp, wow\_rate, wow\_depth\_ms, flutter\_rate, flutter\_depth\_ms, tape\_sat, tape\_age, duck\_threshold, duck\_depth, diffusion); `--interp linear` (default) or `--interp none`; `tape_sat=True` auto-upgrades interp to `"lagrange"` (4th-order, matching C++) |
| `generate_golden.py` | Creates synthetic reference inputs and Python golden WAVs |
| `compare.py` | Diffs `tests/golden/*.wav` against `tests/output/*.wav`; exits non-zero on failure; per-file tolerance overrides for discontinuous modes (bitcrush, foldback) |
| `generate_docs_plots.py` | Generates all PNG plots in `docs/img/` for the effect documentation |
| `tests/test_effects.py` | pytest unit tests for both prototypes: silence-in/silence-out, NaN/Inf bounds, asymmetric clipping, impulse timing, feedback decay |

## C++ Testing (Catch2)

Tests live in `tests/`, one file per effect. Catch2 is installed via `brew install catch2` and found with `find_package(Catch2 3 REQUIRED)`.

```sh
ctest --test-dir build --output-on-failure          # all tests
ctest --test-dir build -R Overdrive                 # overdrive tests (title prefix, not file name)
ctest --test-dir build -R CabinetIR                 # cabinet tests  (not -R test_cabinet — matches nothing)
ctest --test-dir build -R "Delay:"                  # delay tests
ctest --test-dir build -R AnoLookAndFeel            # JUCE LAF tests (requires -DBUILD_JUCE_PLUGIN=ON)
```

CTest's `-R` flag matches against the **registered test name**, which `catch_discover_tests` sets to the Catch2 test case title (e.g. `"CabinetIR: silence in → silence out"`). It does **not** match binary names, file names, or Catch2 `[tags]`. Always filter by title prefix — never by the test file name (e.g. `-R test_cabinet` matches nothing). See `docs/lessons-learned.md` §21a for the full explanation.

`tests/wav_compare.cpp` is a separate CLI tool (not a Catch2 test) that reads an input WAV, runs a named effect with given parameters, and writes the processed output WAV. It is the C++ half of the golden-output pipeline — `make validate` builds it and runs it on every golden input; `make compare` then calls `compare.py` to diff the results. Cabinet supports a `--type` flag: `wav_compare cabinet in.wav out.wav --type 4x12` (or `--type combo`; default is `1x12`). `tests/wav_io.h` is a minimal float32/PCM16 WAV reader+writer used only by `wav_compare`.

JUCE-specific tests live in `juce-plugin/test_laf.cpp` and are built as a separate `laf_tests` executable via `juce_add_console_app`. They require `-DBUILD_JUCE_PLUGIN=ON` and use a custom `main()` that calls `juce::ScopedJuceInitialiser_GUI` before the Catch2 session. The binary is at `build/juce-plugin/laf_tests_artefacts/laf_tests`. Use `ctest -R AnoLookAndFeel` (not `-R laf`) to filter these tests — Catch2 tags are not propagated to CTest test names.

## Architecture

### Shared Effects Library (`libs/effects/`)

The core DSP layer. Header-only; built as a CMake `INTERFACE` target so it links into both plugin targets without a separate compile step.

Every effect implements a common interface:

```cpp
class Effect {
public:
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(float* buffer, int numSamples) = 0;
    virtual void processStereo(float* left, float* right, int numSamples) {
        process(left, numSamples);
        std::copy(left, left + numSamples, right);
    }
    virtual void reset() {}
};
```

Key headers:

| File | Purpose |
| --- | --- |
| `Effect.h` | Abstract base class; `processStereo()` default copies mono result to both channels — override for true stereo |
| `SmoothedValue.h` | One-pole parameter smoother (20 ms ramp) — use this for every knob to prevent zipper noise |
| `DelayLine.h` | Template circular buffer / delay-line — `push(x)`, `read(d)`, `readLerp(d)`, `readLagrange(d)` (4th-order, 5-point); shared by `Delay` and `Oversampler` |
| `BiquadFilter.h` | Second-order IIR biquad filter; `Coeffs` struct + `process()` / `reset()` / `setCoeffs()`; static design methods: `designHighShelf`, `designButterworthLP`, `designButterworthHP`, `designPeaking` (Audio EQ Cookbook formulas); used by `Overdrive` for all its biquads |
| `EnvelopeFollower.h` | Asymmetric peak envelope follower — separate attack/release IIR coefficients (`exp(-1/(sr·τ))`); `prepare(sr, attackMs, releaseMs)` / `process(x)` / `reset()`; used by `Overdrive` (pick sensitivity: 1 ms / 100 ms) and `Delay` (ducking: 5 ms / 500 ms) |
| `OnePoleFilter.h` | First-order low-pass IIR (`OnePoleLP`); `setCutoff(fs, fc)` or `setAlpha(a)`; `process(x)` / `reset()`; used by `Delay` for the feedback LP (fc = 4000 Hz at tapeAge=0, 1500 Hz at tapeAge=1) |
| `AllpassFilter.h` | Schroeder allpass stage; `prepare(D, g)` / `process(x)` / `reset()`; double-precision internal state; used by `Delay` for pre-delay diffusion (four stages at 11, 17, 23, 31 ms; g=0.5) |
| `Oversampler.h` | 4× polyphase FIR upsampler/downsampler; 128-tap Kaiser-windowed LP (β=8, ≥80 dB stopband); used by `Overdrive` |
| `Overdrive.h` | Overdrive with 4× oversampling, five `DistortionType` waveshapers, three `ClipShape` pre/de-emphasis modes, mid peaking EQ (800 Hz), presence high shelf (4 kHz), pick-sensitivity envelope follower, and bias operating-point shift (DC offset before waveshaper) |
| `CabinetIR.h` | Standalone 256-tap minimum-phase FIR speaker cabinet simulation; `CabinetType` enum (`OpenBack1x12`, `ClosedBack4x12`, `Combo1x12`); `setEnabled(bool)` / `setType(CabinetType)`; `buildActiveIR()` resamples the selected 48 kHz base IR to the host sample rate using linear interpolation; direct-form circular-buffer convolution; no-op bypass when disabled |
| `CabinetIR_data.h` / `CabinetIR_data_4x12.h` / `CabinetIR_data_combo.h` | Generated headers (one per cabinet preset); regenerate with `python/cabinet_ir.py --generate-header --type {1x12,4x12,combo}` |
| `Delay.h` | Feedback delay using `DelayLine` with linear interpolation (default) or 4th-order Lagrange (auto-selected when `tapeSat=true` — "tape mode"), 10 ms read-pointer crossfade on time changes (`activeTimeSamples`/`pendingTimeSamples`/`crossfadeCounter`), `OnePoleLP` feedback filter (4 kHz), wow/flutter LFOs (exposed as single `modDepth` knob in both hosts), optional tape saturation (`tapeSat`/`tapeAge`), duck delay (`duckThreshold`/`duckDepth` — `EnvelopeFollower` at 5 ms attack / 500 ms release gates the wet gain), four-stage `AllpassFilter` diffusion, self-oscillation (`selfOscillate` — unlocks feedback ceiling to 1.02 with tape sat active, 0.98 without, preventing runaway via the tanh softclipper), and stereo processing via `processStereo()` in two modes: independent channels (`pingPong=false`, R time = L × 1.02) and ping-pong (`pingPong=true`, cross-channel feedback) |

This library must remain free of JUCE and VCV Rack headers so it links cleanly into both plugin targets.

### JUCE Plugin (`juce-plugin/`)

Wraps the effects library in a `juce::AudioProcessor`. Parameters are exposed via `AudioProcessorValueTreeState` and the editor maps sliders/knobs to those parameters. Target formats: VST3, AU, Standalone. All float parameters in `createParameterLayout()` carry `withStringFromValueFunction` / `withValueFromStringFunction` lambdas: 0–1 knobs display as percentages, dB-range knobs as `±X.X dB`, and delay time as `XXX ms`.

The editor (`PluginEditor.cpp`) is a fixed 600×400 window with a custom `AnoLookAndFeel` (subclass of `juce::LookAndFeel_V4`, defined in `juce-plugin/AnoLookAndFeel.h/.cpp`). The LAF renders rotary knobs as a dark body with a 270° track arc and a filled accent-color value arc, and combo boxes as flat dark fill with an accent-color border. Section accent colors are gold `0xffc8a96e` (Overdrive) and blue `0xff4477aa` (Delay) — matching the VCV Rack panel SVGs. Per-section color is applied with `slider.setColour(rotarySliderFillColourId, accent)` and `combo.setColour(outlineColourId, accent)` after `setLookAndFeel(&anoLnf)`; the LAF's `findColour()` reads these overrides at draw time. Overdrive section: Bypass toggle in the header (`od_bypass`), Drive/Tone/Level/Mid/Presence/Bias knobs (six at 50 px each), Type/Shape/Cabinet Type combos (three rows), Pick Sensitivity toggle (`od_pick_sens`) and Cabinet IR toggle (`od_cabinet`) side-by-side. Delay section: Bypass toggle in the header (`dl_bypass`), Time/Feedback/Mix main knobs, plus Mod Depth/Diffusion/Tape Age/Duck Threshold/Duck Depth secondary knobs (five at 60 px each), Tape Saturation + Self Oscillate toggles side-by-side on the bottom row (each 150 px wide), and a Ping-Pong toggle (`dl_ping_pong`) on the row below. In stereo `processBlock()`, `delay[0].processStereo(L, R, N)` handles both channels. `CabinetIR cabinet[2]` is applied per-channel after overdrive when `od_cabinet` is true; output is hard-clipped to ±1.0 after convolution.

### VCV Rack Module (`vcv-rack/`)

Wraps the same effects library in a `rack::Module` subclass. VCV Rack processes one sample at a time, so `process()` is called per-sample rather than per-block. CV inputs map to effect parameters; audio ports map to the effect's audio I/O.

Sample rate is tracked via `onSampleRateChange()` which calls `effect.prepare()`. CV inputs map ±5 V to ±half the parameter range. Both modules call `configBypass(AUDIO_INPUT, AUDIO_OUTPUT)` so right-click → Bypass passes audio through dry.

| File | Module |
| --- | --- |
| `src/OverdriveModule.cpp` | Overdrive (10HP) — two-column layout: Drive/Tone/Level large knobs at x=15.24 mm, Mid/Presence small knobs + Pick toggle + Bias small knob at x=35.56 mm (y=22/40/58/66 mm); CV row 1 (y=76 mm): DRV/TON/LVL/MID; CV row 2 (y=89 mm): PRS/PCK/BAS/TYPE; SHAPE CV at x=10.16 mm, y=100 mm (left column between row 2 and audio); TYPE CV: 0–8 V → distortion type 0–4; SHAPE CV: 0–4 V → clip shape 0–2; Cabinet CKSS toggle (`CABINET_PARAM`) at x=25.4 mm, y=96 mm labeled "CAB"; cabinet preset (1×12/4×12/combo) via `CABINET_TYPE_PARAM` set through right-click context menu; BYPASS_INPUT gate jack; SIGNAL_LIGHT (green) + CLIP_LIGHT (red) near output |
| `src/DelayModule.cpp` | Delay (10HP) — Time/Feedback/Mix large knobs centred at x=25.4 mm; secondary control pairs flanking at x=10.16 mm (MOD/SAT/DUCK) and x=40.64 mm (DIFF/AGE/DEPTH); Self Osc (OSC) CKSS toggle centred at x=25.4 mm on the duck row (y=93 mm); CV row at y=104 mm; timing inputs row at y=111 mm: TAP (tap tempo), CLK (clock gate → interval), PITCH (V/oct → `1000/(440×2^v)` ms); priority V/OCT > CLK > TAP > knob; R_AUDIO_INPUT (R IN) at x=30.48 mm y=118 mm and R_AUDIO_OUTPUT (R OUT) at x=40.64 mm y=123 mm — stereo-independent mode when R IN connected, mono otherwise; BYPASS_INPUT gate jack; SIGNAL_LIGHT (green) + CLIP_LIGHT (red) near output |
| `res/Overdrive.svg` | 10HP dark panel, gold accent (`#c8a96e`) — labels for all knobs/jacks including MID/PRES/PICK/BIAS/BYP; small circles for SIGNAL/CLIP lights |
| `res/Delay.svg` | 10HP dark panel, blue accent (`#6e9ac8`) — labels for all knobs/jacks including MOD/DIFF/SAT/AGE/DUCK/OSC/DEPTH/TAP/CLK/V/OCT/BYP; small circles for SIGNAL/CLIP lights |

Known gaps: Wow/flutter depth is exposed via the Mod Depth knob (rates are fixed); Mod Depth and Diffusion CV inputs not yet in VCV Rack (TODO 20n); Delay ping-pong mode not exposed as a panel control — stereo always uses independent mode (TODO 20m); both 10HP panels are at layout capacity — expansion to 16HP (TODO 20o/20p) is a prerequisite for cabinet type CV on Overdrive (TODO 20q) and Delay secondary parameter CV inputs — tape age, duck depth, self-osc gate (TODO 20r).

### Python Prototypes (`python/`)

Each effect has a self-contained Python module (numpy/scipy) that accepts a WAV file and writes processed output. These scripts produce the "golden" WAV files used to validate C++ output. The acceptable max sample-level error between Python golden and C++ output is **5e-4** — float32 biquad state vs float64 sosfilt sets this floor; any real algorithm bug produces errors ≫ 1e-3.

## Color Scheme

Each effect owns one accent color used in both its VCV Rack SVG and its JUCE editor section. Use these values whenever touching SVG `fill` attributes or `juce::Colour` literals.

| Effect | VCV SVG | JUCE (`0xffRRGGBB`) |
| --- | --- | --- |
| Overdrive | `#c8a96e` (gold) | `0xffc8a96e` |
| Delay | `#6e9ac8` (blue) | `0xff4477aa` — slightly darker than the SVG value; aligning them is TODO 21j |
| Tremolo (planned §24) | `#44aa88` (teal) | `0xff44aa88` |
| Compressor (planned §25) | `#aa8844` (amber) | `0xffaa8844` |

Shared neutral values: panel background `#1a1a2e`, primary knob labels `#aaa`, CV/port labels `#888`, section markers `#555`, module subtitle `#666`. JUCE-only: divider line `0xff333355`, row separator `0xff222244`. The audio OUT label in each SVG uses the module's accent color; IN uses `#888`.

## Key Conventions

- Prototype every effect in Python first; validate against the C++ implementation before shipping.
- The `libs/effects/` library is the source of truth for DSP behavior — JUCE and VCV Rack wrappers must not reimplement algorithm logic.
- Brew is the preferred package manager for all development tools.
- Before writing DSP unit tests or implementing new filters, check `docs/lessons-learned.md` — it records post-mortems for known design traps: output formula errors, test-frequency GCD selection, frequency-domain normalization, and PCM-16/float32 representation mismatches.
