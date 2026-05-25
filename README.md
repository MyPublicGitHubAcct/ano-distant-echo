# Distant-Echo

Guitar effects emulator — shared C++ DSP library used by both a JUCE audio plugin and a VCV Rack module. Effects are prototyped in Python before being implemented in C++.

## Current Status

Both effects are functional and fully tested. Known gaps before production use:

| Item | Status |
| ---- | ------ |
| JUCE plugin — manual DAW testing (VST3/AU with a guitar DI) | Not yet done |
| VCV Rack modules — manual testing with audio signals in Rack | Not yet done |
| Cabinet IR convolution (three presets: 1×12, 4×12, 1×12 combo) | Done (TODO 16g) — JUCE `od_cabinet` + `od_cabinet_type`; VCV Rack panel CKSS toggle + right-click menu for preset |
| JUCE: clip/activity indicator lights in editor | Not yet (TODO 21d) |
| JUCE: per-effect bypass buttons in editor | Done (TODO 21c) |
| JUCE: custom LookAndFeel (colored knobs/arcs) | Done (TODO 21a) |
| VCV Rack: signal/clip indicator lights on both modules | Done (TODO 20c) |
| VCV Rack: bypass CV gate jack (BYP) on both modules | Done (TODO 20h) |
| VCV Rack: Delay tap tempo, clock, and V/oct inputs | Done (TODO 20e/20f) |
| VCV Rack: Overdrive distortion type and shape CV inputs | Done (TODO 20d) — TYPE CV (0–8 V → type 0–4) in CV row 2; SHAPE CV (0–4 V → shape 0–2) at y=100 mm |
| VCV Rack: both panels widened to 16HP | Not yet (TODO 20o/20p) — current 10HP layouts are at capacity |
| VCV Rack: Cabinet Type CV input for Overdrive | Not yet (TODO 20q) — requires 16HP reflow first |
| VCV Rack: Delay secondary parameter CV inputs (tape age, duck depth, self-osc gate) | Not yet (TODO 20r) — requires 16HP reflow first |
| Delay blue color: SVG `#6e9ac8` vs JUCE `#4477aa` | Mismatch (TODO 21j) |
| Self-oscillation mode on Delay | Done (TODO 14g) |
| Reverb, Tremolo, Compressor effects | Planned (§23–25) |

## Prerequisites (macOS)

### 1. Xcode Command Line Tools

```sh
xcode-select --install
```

Provides Clang 15+ and the macOS SDK. Required for all C++ compilation.

### 2. Homebrew

If not already installed, follow the instructions at [brew.sh](https://brew.sh). Then install the required tools:

```sh
brew install cmake ninja catch2 uv ruff zstd
brew install --cask pluginval   # JUCE VST3/AU validation (optional, macOS only)
```

| Tool | Purpose |
| ------ | --------- |
| cmake 3.22+ | Build system |
| ninja | Build backend |
| catch2 3.x | C++ unit test framework |
| uv | Python environment and package manager |
| ruff | Python linter |
| zstd | Compression (VCV Rack `.vcvplugin` packaging only) |
| pluginval | Tracktion's plugin contract validator (VST3/AU) |

Python 3.12+ is managed automatically by `uv` — no separate install needed.

`auval` (Apple's AU Validator) ships with Xcode Command Line Tools — no separate install needed.

### 3. VCV Rack SDK (for the Rack module only)

1. Download `Rack-SDK-2.6.x-mac-arm64.zip` from the [VCV Rack downloads page](https://vcvrack.com/Rack).
2. Unzip it to `~/Rack-SDK` (CMake looks there by default).

## Getting Started

```sh
git clone <repo-url>
cd ano-distant-echo
git submodule update --init libs/juce   # only needed if building the JUCE plugin
```

## Building

```sh
# Configure and build — effects library + Catch2 tests (no SDKs required)
# cmake --build also compiles the Catch2 test binaries when catch2 is installed
cmake -B build -G Ninja
cmake --build build

# Run all C++ unit tests
ctest --test-dir build --output-on-failure
```

```sh
# JUCE plugin (requires libs/juce submodule)
cmake -B build -G Ninja -DBUILD_JUCE_PLUGIN=ON
cmake --build build --target Distant-Echo_Standalone   # or Distant-Echo_VST3
```

```sh
# VCV Rack plugin
cmake -B build -G Ninja                              # ~/Rack-SDK used by default
cmake --build build --target Distant-EchoRack          # build plugin.dylib only
cmake --build build --target install-rack-plugin     # build + install to Rack2 plugins dir
cmake --build build --target dist-rack-plugin        # build distributable .vcvplugin archive
```

```sh
# All three targets simultaneously
cmake -B build -G Ninja -DBUILD_JUCE_PLUGIN=ON      # ~/Rack-SDK used by default
cmake --build build
```

## Installing (macOS)

### JUCE Plugin

```sh
cmake -B build -G Ninja -DBUILD_JUCE_PLUGIN=ON
cmake --build build --target install-juce
```

`install-juce` installs all three formats. To install formats individually:

```sh
cmake --build build --target install-vst3        # → ~/Library/Audio/Plug-Ins/VST3/Distant-Echo.vst3
cmake --build build --target install-au          # → ~/Library/Audio/Plug-Ins/Components/Distant-Echo.component
cmake --build build --target install-standalone  # → ~/Applications/Distant-Echo.app
```

**VST3** — Trigger a plugin rescan in your DAW after installing (Ableton: Preferences → Plug-Ins → Rescan; Logic rescans on launch).

**AU** — The install target kills `AudioComponentRegistrar` so macOS rebuilds its AU cache immediately. Restart any open DAW, then verify with `auval -v aufx Anoe Anoe` before loading in Logic Pro or GarageBand.

**Standalone** — Launch directly from `~/Applications/Distant-Echo.app`. No DAW required.

### VCV Rack Module

```sh
cmake -B build -G Ninja                              # ~/Rack-SDK used by default
cmake --build build --target install-rack-plugin
# → ~/Library/Application Support/Rack2/plugins-mac-arm64/Distant-Echo/
```

Restart VCV Rack after installing.

## C++ Tests

Catch2 test binaries are compiled by `cmake --build build` when `catch2` is installed (see Prerequisites). Build first, then run with `ctest`:

```sh
ctest --test-dir build --output-on-failure          # all Catch2 tests
ctest --test-dir build -R Overdrive                 # overdrive tests (title prefix, not file name)
ctest --test-dir build -R CabinetIR                 # cabinet tests  (not -R test_cabinet — matches nothing)
ctest --test-dir build -R "Delay:"                  # delay tests
ctest --test-dir build -R AnoLookAndFeel            # JUCE LookAndFeel tests (requires -DBUILD_JUCE_PLUGIN=ON)
```

CTest's `-R` flag matches against the **registered test name**, which `catch_discover_tests` sets to the Catch2 test case title. It does not match binary names, file names, or Catch2 `[tags]` — filter by title prefix only. For example, `-R test_cabinet` matches nothing; `-R CabinetIR` selects all cabinet tests.

The JUCE LookAndFeel tests (`juce-plugin/test_laf.cpp`) are a separate `laf_tests` target built only when `-DBUILD_JUCE_PLUGIN=ON`. They use a custom `main()` that calls `juce::ScopedJuceInitialiser_GUI` before the Catch2 session. Use `-R AnoLookAndFeel` to filter them — `-R laf` matches nothing. See `docs/lessons-learned.md` §21a for the full explanation.

## Plugin Validation (JUCE only)

Requires the JUCE plugin to be built first (`-DBUILD_JUCE_PLUGIN=ON`).

```sh
# VST3 — Tracktion pluginval, strictness level 5
pluginval --strictness-level 5 --validate-in-process \
    build/juce-plugin/Distant-Echo_artefacts/VST3/Distant-Echo.vst3

# AU — Apple auval (ships with Xcode Command Line Tools)
auval -v aufx Anoe Anoe
```

`pluginval` exercises audio processing, state save/restore, automation, editor creation, and bus layouts at multiple sample rates and block sizes. `auval` is required before loading the AU component in Logic Pro or GarageBand.

## Cleaning

```sh
# Full clean — removes the build directory and all CMake cache state
rm -rf build

# Reconfigure from scratch after cleaning
cmake -B build -G Ninja
```

A full clean is necessary when switching between configurations (e.g. adding or removing `-DBUILD_JUCE_PLUGIN=ON`), because CMake caches option values and a stale cache can cause configure errors.

## JUCE Setup

JUCE lives in `libs/juce` as a git submodule. Initialize it once after cloning:

```sh
git submodule update --init libs/juce
```

To use a different local clone, pass `-DJUCE_DIR=/path/to/JUCE` to CMake.

## Python Tests

Python unit tests (pytest) cover both prototype modules and are independent of the C++ build:

```sh
uv run --project python pytest            # run all Python unit tests
ruff check python/                        # lint
```

## Golden WAV Validation

Golden WAVs are the reference outputs produced by the Python prototypes. They must be generated once (and regenerated after any algorithm change) before the C++ validation can run. The `make validate` step builds `wav_compare` via CMake if needed.

```sh
make golden        # generate tests/golden/input/*.wav + tests/golden/*.wav from Python
make validate      # build wav_compare (C++), run it on every golden input → tests/output/
make compare       # validate + diff every golden WAV against its C++ output (exits non-zero on mismatch)
make clean-output  # remove tests/output/
```

`make golden` is Python-only and requires no prior C++ build. `make compare` depends on `make validate`, which triggers a CMake build of `wav_compare` automatically.

## Docs Plot Generation

PNG plots in `docs/img/` are not generated automatically during the CMake build. Regenerate them explicitly after changing any effect algorithm or documentation:

```sh
uv run --project python python/generate_docs_plots.py    # regenerate all docs/img/ PNG plots
```

## Python Environment

```sh
uv run --project python python/overdrive.py in.wav out.wav --drive 0.7 --mode softclip --plot
uv run --project python python/delay.py in.wav out.wav --time-ms 300 --feedback 0.4
uv add --project python <package>                         # add a Python dependency
```

## Project Layout

```text
libs/effects/      shared C++ DSP library (header-only, no JUCE or VCV Rack headers)
libs/juce/         JUCE git submodule
juce-plugin/       JUCE AudioProcessor + editor
vcv-rack/          VCV Rack module
python/            effect prototypes and golden-output validation scripts
python/tests/      pytest unit tests for the Python prototypes
tests/             Catch2 unit tests + WAV validation tooling
tests/golden/      reference input WAVs (input/) and expected output WAVs
tests/output/      C++ output WAVs written by wav_compare (git-ignored)
docs/              Markdown documentation and DSP plots for libs/effects
```

## Implemented Effects

| Effect | C++ | Python prototype |
| --- | --- | --- |
| Overdrive | `libs/effects/Overdrive.h` | `python/overdrive.py` |
| ↳ waveshaper | HardClip, SoftClip, Foldback, Asymmetric, Bitcrush (`--mode`) | |
| ↳ clip shape | Flat, MidFocus (+6 dB HS @ 700 Hz), BrightFocus (+6 dB HS @ 3 kHz) | |
| ↳ features | 4× oversampling, mid EQ (800 Hz), presence shelf (4 kHz), pick sensitivity, bias (operating-point shift) | |
| Cabinet IR | `libs/effects/CabinetIR.h` | `python/cabinet_ir.py` |
| ↳ presets | 1×12 open-back (+6 dB resonance at 120 Hz, −6 dB/oct above 4 kHz); 4×12 closed-back (+6 dB at 80 Hz, −3 dB/oct above 3 kHz); 1×12 combo (+6 dB at 180 Hz, −6 dB/oct above 5 kHz) | |
| ↳ features | `CabinetType` enum; `buildActiveIR()` resamples 48 kHz base IR to host sample rate; direct-form circular-buffer convolution; true no-op bypass; applied after `Overdrive`, before ±1.0 hard-clip ceiling | |
| Delay | `libs/effects/Delay.h` | `python/delay.py` |
| ↳ interpolation | Linear (default / Digital mode); 4th-order Lagrange auto-selected when `tapeSat=true` (Tape mode) — better HF response at fractional delays | |
| ↳ features | `DelayLine`-backed circular buffer, 10 ms crossfade on time changes, `OnePoleLP` feedback filter (4 kHz), wow/flutter LFOs (single Mod Depth knob in both hosts at fixed rates 0.5/8 Hz), tape saturation + `tapeAge`, duck delay with `duckThreshold`/`duckDepth` (`EnvelopeFollower` at 5 ms / 500 ms), four-stage `AllpassFilter` pre-delay diffusion, self-oscillation (`selfOscillate` — unlocks feedback to 1.02 with tape sat, 0.98 without), stereo (`processStereo()`) — independent channels (R time = L × 1.02) or ping-pong (cross-channel feedback, `pingPong=true`) | |

## Planned Effects

The project goal is a full circuit-emulating effects suite. All planned effects follow the same Python-first, golden-WAV validation workflow as the current effects and will share `libs/effects/`.

| Effect | Category | Algorithm target | TODO |
| --- | --- | --- | --- |
| Reverb | Time-based | Spring reverb (FDN + allpass + coil ripple notch) and plate reverb (six-line FDN) | Section 23 |
| Tremolo | Modulation | Optical (LDR photocell) and harmonic (two-band out-of-phase) tremolo | Section 24 |
| Compressor | Dynamics | Soft-knee RMS compressor with look-ahead and limiter mode | Section 25 |
| Chorus / Flanger | Modulation | BBD-style chorus (TODO 19g) and comb-filter flanger (TODO 19b), both as Delay extended modes | — |

Advanced extensions to the current Overdrive and Delay — tape saturation, wow/flutter, duck delay, diffusion, power-supply sag, gated/octave fuzz, multi-tap and pitch-shifted delay, self-oscillation — are tracked in TODO sections 14–19. Fractional-delay linear interpolation (14a), 4th-order Lagrange interpolation auto-selected in tape mode (14a), read-pointer crossfade (14b), Mod Depth knob for wow/flutter at fixed rates (14c), tape saturation with `tapeAge` (14d), duck delay with `duckThreshold`/`duckDepth` (14e), pre-delay allpass diffusion (14f), self-oscillation with `selfOscillate` (14g), and bias operating-point shift (16a) are implemented.

## Color Scheme

Each effect has a unique accent color used consistently across the VCV Rack SVG panel and the JUCE plugin editor. When adding a new effect, pick a new hue from the same muted-on-dark-navy family and register it in both places.

### Per-effect accent colors

| Effect | VCV SVG hex | JUCE `0xffRRGGBB` | Hue |
| --- | --- | --- | --- |
| Overdrive | `#c8a96e` | `0xffc8a96e` | Gold |
| Delay | `#6e9ac8` | `0xff4477aa` | Blue — SVG and JUCE values differ slightly (see note below) |
| Tremolo (planned §24) | `#44aa88` | `0xff44aa88` | Teal |
| Compressor (planned §25) | `#aa8844` | `0xffaa8844` | Amber |

### Shared panel / editor colors

| Role | Value | Where |
| --- | --- | --- |
| Panel background | `#1a1a2e` | Both SVGs + JUCE `0xff1a1a2e` |
| Primary knob labels | `#aaa` | SVG `fill` |
| Secondary / CV labels | `#888` | SVG `fill` |
| Section markers ("CV") | `#555` | SVG `fill` |
| Module subtitle ("ANO") | `#666` | SVG `fill` |
| Vertical divider | — | JUCE `0xff333355` |
| Row separator | — | JUCE `0xff222244` |

In each SVG the audio **OUT** label uses the module's accent color; **IN** uses `#888`. The accent color also applies to the top/bottom panel strips and the module title text.

> **Delay blue discrepancy:** the VCV SVG uses `#6e9ac8` (the original design value) while the JUCE editor uses `#4477aa` (chosen for legibility against the dark background). Aligning them is tracked as TODO 21j.

## JUCE Plugin Editor

The editor is a fixed 600×400 two-section window divided by a vertical line with gold (Overdrive) and blue (Delay) header labels matching the VCV Rack panel color scheme.

**Left section — Overdrive:** Bypass toggle in the section header; Type, Clip Shape, and Cabinet Type combo boxes; Drive / Tone / Level / Mid / Presence / Bias knobs (six at 50 px each); Pick Sensitivity toggle and Cabinet IR toggle side-by-side.

**Right section — Delay:** Bypass toggle in the section header; Row 1 — Time / Feedback / Mix knobs. Row 2 — Mod Depth / Diffusion / Tape Age / Duck Threshold / Duck Depth knobs (five at 60 px each). Bottom row — Tape Saturation and Self Oscillate toggles side-by-side; Ping-Pong toggle below. All float knob text boxes show formatted values (percentages for 0–1 knobs, `dB` for EQ and duck parameters, `ms` for delay time).

Parameters exposed via `AudioProcessorValueTreeState`:

| ID | Range | Default | Description |
| --- | --- | --- | --- |
| `od_type` | 0–4 (choice) | 0 | Distortion type: Hard Clip / Soft Clip / Foldback / Asymmetric / Bitcrush |
| `od_shape` | 0–2 (choice) | 0 | Pre/de-emphasis: Flat / Mid Focus / Bright Focus |
| `od_drive` | 0–1 (%) | 0.5 | Pre-amp gain into waveshaper |
| `od_tone` | 0–1 (%) | 0.5 | Tone blend (LP ↔ HP crossfade) |
| `od_level` | 0–1 (%) | 0.8 | Output level scalar |
| `od_mid` | −6–+10 dB | 0 | Peaking mid EQ at 800 Hz |
| `od_presence` | 0–+8 dB | 0 | High shelf at 4 kHz |
| `od_bias` | ±0.5 | 0 | DC offset before waveshaper; generates even-order harmonics from symmetric clippers |
| `od_pick_sens` | bool | true | Enable pick-sensitivity envelope gain reduction |
| `od_cabinet` | bool | false | Enable cabinet IR convolution after the overdrive signal chain |
| `od_cabinet_type` | choice (0–2) | 0 | Cabinet preset: 0 = 1×12 open-back, 1 = 4×12 closed-back, 2 = 1×12 combo |
| `od_bypass` | bool | false | Bypass overdrive — passes audio dry and resets effect state |
| `dl_time` | 1–2000 ms | 300 | Delay time (skewed range) |
| `dl_feedback` | 0–1.02 (%) | 0.4 | Feedback ratio; values above 0.95 only take effect when `dl_self_oscillate` is on |
| `dl_mix` | 0–1 (%) | 0.5 | Wet/dry blend |
| `dl_mod_depth` | 0–1 (%) | 0 | Wow + flutter depth at fixed rates (wow 0.5 Hz → 0–4 ms; flutter 8 Hz → 0–1 ms) |
| `dl_diffusion` | 0–1 (%) | 0 | Pre-delay allpass diffusion (four 11/17/23/31 ms stages) |
| `dl_tape_sat` | bool | false | Enable tape saturation in feedback path |
| `dl_tape_age` | 0–1 (%) | 0 | Tape age: darkens LP cutoff (4 kHz → 1.5 kHz) and raises saturation drive |
| `dl_duck_threshold` | −30–0 dB | 0 | Duck threshold; at 0 dB ducking is effectively disabled |
| `dl_duck_depth` | 0–1 (%) | 0 | Duck depth; 1 = wet fully muted while dry input exceeds threshold |
| `dl_self_oscillate` | bool | false | Unlocks feedback above 0.95 — up to 1.02 with tape sat (tanh prevents runaway), 0.98 without |
| `dl_ping_pong` | bool | false | Ping-pong stereo (echoes alternate L/R); independent stereo when off (R time = L × 1.02) |
| `dl_bypass` | bool | false | Bypass delay — passes audio dry and resets effect state |

## VCV Rack Modules

Both modules share a dark `#1a1a2e` panel with accent strips and title text at top and bottom. Audio IN is at bottom-left, audio OUT at bottom-right. Both call `configBypass(AUDIO_INPUT, AUDIO_OUTPUT)` so right-click → Bypass passes audio through dry.

**Overdrive** (10HP, gold accent `#c8a96e`): Two-column layout — Drive / Tone / Level large knobs in the left column (x=15.24 mm), Mid / Presence small knobs + Pick Sensitivity toggle + Bias small knob in the right column (x=35.56 mm, y=22/40/58/66 mm). CV row 1 (y=76 mm): Drive / Tone / Level / Mid — four jacks. CV row 2 (y=89 mm): Presence / Pick / Bias / Type — four jacks; TYPE CV (0–8 V) snaps to distortion type 0–4. SHAPE CV at x=10.16 mm, y=100 mm (isolated row between CV row 2 and the audio section); SHAPE CV (0–4 V) snaps to clip shape 0–2. Cabinet IR CKSS toggle at x=25.4 mm, y=96 mm ("CAB"); cabinet preset (1×12/4×12/combo) via right-click context menu. Distortion Type and Clip Shape also settable via right-click context menu. Bottom row: IN / BYP / OUT — BYP is a gate jack (> 1 V passes audio dry and resets effect state). Green SIGNAL and red CLIP indicator lights appear near the output jack.

**Delay** (10HP, blue accent `#6e9ac8`): Three large knobs (Time, Feedback, Mix) centred at x=25.4 mm. Secondary controls flank the large knobs at x=10.16 mm (left) and x=40.64 mm (right) — Mod Depth / Saturation toggle / Duck Threshold on the left; Diffusion / Tape Age / Duck Depth on the right. Self Oscillate (OSC) CKSS toggle centred at x=25.4 mm on the duck controls row (y=93 mm). One CV row — Time / Feedback / Mix CV (y=104 mm). Timing inputs row (y=111 mm): TAP / CLK / V/OCT. TAP and CLK both use rising-edge detection (≥ 1 V); two pulses establish the interval (10–4000 ms). V/OCT maps 1 V/oct to delay time via `1000 / (440 × 2^v)` ms. Priority: V/OCT > CLK > TAP > TIME knob; TIME CV offset is suppressed when V/OCT is connected. Bottom row: L IN / BYP / R IN / L OUT — R IN (x=30.48 mm, y=118 mm) enables stereo-independent mode when connected; R OUT (x=40.64 mm, y=123 mm) carries the right channel. BYP is a gate jack (> 1 V passes both channels dry and resets effect state). Green SIGNAL and red CLIP indicator lights appear near the output jack.

| Module param | CV range | Effect on parameter |
| --- | --- | --- |
| Drive / Tone / Level / Mix | ±5 V | ±0.5 offset (clamped to param range) |
| Mid | ±5 V | ±8 dB offset |
| Presence | ±5 V | ±4 dB offset |
| Bias | ±5 V | ±0.5 offset (clamped to ±0.5) |
| Distortion Type (Overdrive) | 0–8 V | snaps to type index 0–4 when connected: 0 V = Hard Clip, ~2 V = Soft Clip, ~4 V = Foldback, ~6 V = Asymmetric, ~8 V = Bitcrush |
| Clip Shape (Overdrive) | 0–4 V | snaps to shape index 0–2 when connected: 0 V = Flat, ~1.3 V = Mid Focus, ~2.7 V = Bright Focus |
| Pick Sensitivity | gate > 1 V | overrides the panel toggle when jack is connected |
| Time | ±5 V | ±1000 ms offset |
| Feedback | ±5 V | ±0.51 offset (clamped to 0–0.95 normally; 0–1.02 when Self Oscillate is on) |

## Documentation

Detailed DSP documentation with frequency-response and transfer-curve plots lives in [docs/](docs/):

| Document | Covers |
| --- | --- |
| [docs/JUCEPlugin.md](docs/JUCEPlugin.md) | JUCE plugin — signal chain, all parameters, editor layout, validation |
| [docs/VCVRack.md](docs/VCVRack.md) | VCV Rack modules — panel layout, CV ranges, timing inputs, bypass behavior |
| [docs/Design.md](docs/Design.md) | Design workflow, golden-WAV validation pipeline, and testing strategy |
| [docs/Effect.md](docs/Effect.md) | Abstract `Effect` base class interface |
| [docs/SmoothedValue.md](docs/SmoothedValue.md) | One-pole parameter smoother |
| `libs/effects/DelayLine.h` | Template circular buffer — `push`, `read`, `readLerp`, `readLagrange` (4th-order); used by `Delay` and `Oversampler` |
| `libs/effects/BiquadFilter.h` | Second-order IIR biquad — `Coeffs` struct, `process`/`reset`/`setCoeffs`; static design methods for high shelf, Butterworth LP/HP, and peaking EQ (Audio EQ Cookbook); used by `Overdrive` |
| `libs/effects/EnvelopeFollower.h` | Asymmetric peak envelope follower with separate attack/release IIR coefficients; used by `Overdrive` (pick sensitivity) and `Delay` (ducking) |
| `libs/effects/OnePoleFilter.h` | First-order LP (`OnePoleLP`) with `setCutoff`/`setAlpha` API; used by `Delay` feedback path |
| `libs/effects/AllpassFilter.h` | Schroeder allpass stage — double-precision state, `prepare(D, g)`/`process`/`reset`; used by `Delay` diffusion chain |
| [docs/Oversampler.md](docs/Oversampler.md) | 4× polyphase FIR oversampler |
| [docs/Overdrive.md](docs/Overdrive.md) | Full Overdrive signal chain, waveshapers, EQ |
| [docs/CabinetIR.md](docs/CabinetIR.md) | Cabinet IR design, direct-form convolution, normalization |
| [docs/Delay.md](docs/Delay.md) | Feedback delay with LP darkening |
| [docs/lessons-learned.md](docs/lessons-learned.md) | Post-mortems: IR normalization, PCM-16 clipping mismatch, FIR index arithmetic, allpass settling time, OnePoleLP gain tolerance, oversampling aliasing GCD choice (§13a), allpass output formula v[n] vs x[n] (§14f), UTF-8 escape sequences in C++ string literals (§16g) |

## Prompt Notes

### Pattern 1

Even numbered messages, ```/compact```, are optional. In some cases, ```/clear``` may be better.

``` text
Message 1: Do <activity>. Where appropriate, make separate classes. Create unit tests and ensure they are well designed to identify any problems.

Message 2: /compact

Message 3: If any errors or failures were experienced, describe how it was identified, what caused it, and how it was fixed in the lessons-learned.md document in docs.

Message 4: Add any TODO items that are likley to be needed.

Message 5: /compact

Message 6: Update docs, README, CLAUDE, and TODO.
```
