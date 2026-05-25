# Design and Testing Overview

This document describes the workflow used to design, implement, and validate the effects in this project — from initial prototype through golden-output validation and C++ unit testing.

---

## Project Goal

The long-term goal is a production-ready guitar effects suite: a library of circuit-emulating DSP effects available as both a JUCE audio plugin (VST3/AU/Standalone) and a VCV Rack module, sharing a single C++ implementation in `libs/effects/`.

**Current scope:** Overdrive and Delay. **Intended scope:** a full effects chain — distortion (overdrive, fuzz, bitcrusher variants), time-based (delay with tape and BBD character, reverb), modulation (chorus, flanger, tremolo), dynamics (compressor, noise gate, optical compander), and signal shaping (cabinet IR convolution, harmonic exciter) — each targeting circuit-emulation fidelity rather than textbook approximations. The Overdrive currently emulates asymmetric diode/transistor saturation curves and includes bias, power-supply sag, and pick-sensitivity dynamics; the Delay targets tape and BBD analog character with tape saturation, wow/flutter, diffusion, and extended creative modes. Future effects follow the same emulation-over-approximation standard.

**Platform targets:**

- **JUCE plugin** — VST3, AU, and Standalone, with full DAW automation via `AudioProcessorValueTreeState` and parameter smoothing on every continuously-varying control.
- **VCV Rack module** — one module per effect, with CV inputs on every parameter and Eurorack-idiomatic features (tap tempo, gate bypass, V/oct pitch input, clock sync).

Both targets share `libs/effects/` with no algorithm logic duplicated in the plugin wrappers. This constraint ensures that a fix confirmed in C++ and validated against golden WAVs is immediately available in both hosts — the workflow below exists to uphold that guarantee.

---

## Workflow: Python First, C++ Second

Every effect begins as a self-contained Python module in `python/`. The Python prototype is the canonical definition of the DSP algorithm. Only after it produces correct-sounding output is it ported to C++. This ordering has two benefits:

1. **Rapid iteration.** NumPy, SciPy, and matplotlib let you design filters, plot spectra, and listen to results in seconds without a compile cycle.
2. **A reference oracle.** The Python output becomes the "golden" WAV file that the C++ implementation must match sample-for-sample.

The C++ implementation in `libs/effects/` is considered correct only after it passes both the behavioural Catch2 tests and the golden-WAV comparison at the prescribed tolerance.

---

## Effect Interface

Every effect in `libs/effects/` implements the abstract base defined in [Effect.h](../libs/effects/Effect.h):

```cpp
class Effect {
public:
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(float* buffer, int numSamples) = 0;
    virtual void reset() {}
};
```

**`prepare()`** is called before audio streaming begins and whenever the host changes sample rate or block size. Implementations compute filter coefficients, allocate delay lines, and zero internal state. Calling it again is a full reset.

**`process()`** operates in-place on the caller's buffer. The JUCE plugin calls it once per block; the VCV Rack module calls it per sample (block size = 1).

**`reset()`** clears internal state (delay lines, filter states) without recomputing coefficients. Provided with a default no-op; subclasses override only when they hold persistent state.

---

## Parameter Smoothing

All continuously-varying parameters use `SmoothedValue` (a one-pole exponential ramp with a 20 ms time constant). This prevents zipper noise — the audible stepping artifact that occurs when a parameter value changes abruptly between audio blocks.

For parameters that also determine filter coefficients (`mid`, `presence` in `Overdrive`), the smoothed value drives a biquad redesign once per `process()` call when the value has changed. The state is **not** reset on redesign — the biquad settles naturally through incremental coefficient shifts, avoiding the transient glitch that a simultaneous coefficient change and state flush would produce.

---

## 4× Oversampling

Nonlinear waveshapers generate harmonics above the Nyquist frequency. Without intervention, those harmonics alias back into the audio band as inharmonic artefacts. The `Oversampler` addresses this by:

1. **Upsampling** the input by 4× (192 kHz when the host rate is 48 kHz) using a 128-tap Kaiser-windowed low-pass FIR (β = 8, cutoff at Nyquist/4).
2. **Running the waveshaper** at the oversampled rate, where harmonics extend to 96 kHz.
3. **Downsampling** back to the host rate through the same FIR prototype, attenuating all content above 24 kHz by ≥ 80 dB before any alias can fold back into the audio band.

The Kaiser window (β = 8) gives approximately 80 dB minimum stopband attenuation with 128 taps and is fully closed-form — no iterative optimisation — making it straightforward to match exactly between Python (`scipy.signal.firwin`) and C++.

The upsample path uses polyphase decomposition: the 128-tap prototype is split into 4 phases of 32 taps. Each input sample produces four oversampled outputs, one per phase. The downsample path uses direct decimation, computing 128 multiply-adds once per four oversampled samples.

For full oversampler documentation see [Oversampler.md](Oversampler.md).

---

## Golden-WAV Validation

The golden pipeline closes the loop between the Python prototype and the C++ implementation.

### Generating golden files

```sh
make golden
```

This runs `python/generate_golden.py`, which:

1. Writes a set of reference mono-WAV inputs to `tests/golden/input/` — single-frequency sines at multiple amplitudes, a low-frequency sine, and a power chord.
2. Runs each effect through the Python prototype with a fixed parameter set.
3. Writes the Python output to `tests/golden/<name>.wav`.

The parameter sets are chosen to exercise each major code path (all five `DistortionType` modes, both `ClipShape` shelves, mid/presence EQ, etc.).

### Running C++ against the same inputs

```sh
make validate
```

This builds `tests/wav_compare` — a standalone CLI tool that reads a named golden input, runs the C++ effect with matching parameters, and writes the result to `tests/output/<name>.wav`.

### Diffing the outputs

```sh
make compare
```

This calls `python/compare.py`, which reads every pair of golden and output WAVs and checks that the maximum sample-level error is within tolerance:

| Tolerance                | Applies to             |
|--------------------------|------------------------|
| 5 × 10⁻⁴                 | All modes              |
| Looser per-file override | `bitcrush`, `foldback` |

`bitcrush` and `foldback` use a looser per-file override — discontinuous modes where quantisation step boundaries can shift by one sample.

The floor of 5 × 10⁻⁴ comes from using float32 biquad state in C++ against float64 `sosfilt` in Python. Real algorithm bugs produce errors several orders of magnitude larger.

---

## C++ Unit Tests (Catch2)

Catch2 tests live in `tests/`, one file per effect. They are not golden-output tests — they test DSP properties and edge cases directly in C++ without reference WAVs.

### CTest filter patterns

`catch_discover_tests` registers each Catch2 test case by its **title string**, not by the binary name or tag. CTest's `-R` flag is a regex over that title. Consequences:

- `-R test_cabinet` — matches nothing; `test_cabinet` is a file name, not a title prefix
- `-R CabinetIR` — matches all cabinet tests (`CabinetIR: ...` titles)
- `-R Overdrive` — matches all overdrive tests
- `-R "Delay:"` — matches all delay tests
- `-R AnoLookAndFeel` — matches all JUCE LAF tests (requires `-DBUILD_JUCE_PLUGIN=ON`)

Design test case titles with a shared, stable prefix (`"CabinetIR: ..."`, `"Overdrive: ..."`) so that `ctest -R <Prefix>` selects exactly one suite. Use Catch2 `[tags]` for sub-grouping within the binary, but rely on the title prefix for CTest integration. See `lessons-learned.md` §21a for the full post-mortem.

JUCE-specific tests live in `juce-plugin/test_laf.cpp`, built as a separate `laf_tests` console app via `juce_add_console_app`. They require `-DBUILD_JUCE_PLUGIN=ON` and use a custom `main()` that calls `juce::ScopedJuceInitialiser_GUI` before the Catch2 session (JUCE must be initialised before any `juce::Colour` or font code runs in test setup). The binary is at `build/juce-plugin/laf_tests_artefacts/laf_tests`.

### Behavioural tests

| Test | What it verifies |
| ---- | ---------------- |
| Silence in → silence out | No energy is injected at rest |
| Output bounded within [−1, 1] | Waveshaper enforces amplitude ceiling even for heavily overdriven inputs |
| No NaN/Inf at extreme parameter values | Numerical stability across the full parameter space |
| Bypass: drive=0, level=1 produces non-zero output | The pass-through path reaches the output |
| Asymmetric clipping check | `Asymmetric` mode produces different RMS on positive and negative half-cycles |
| Bitcrush quantisation levels | At drive=1, output contains only the expected discrete amplitude levels |
| Pick sensitivity lowers peak gain | With `pickSens` enabled, loud transients are attenuated relative to quiet passages |
| Delay: impulse timing | A single-sample impulse appears at the delay tap at the expected sample offset |
| Delay: feedback decay | Successive echoes decrease by the expected factor |

Tests use `GENERATE` to run across multiple sample rates (44100, 48000, 96000 Hz) and parameter combinations in a single `TEST_CASE`.

### Aliasing and spectral tests

The oversampling test checks that content above 20 kHz is attenuated by at least 80 dBFS after a hard-clipped sine passes through the waveshaper at full drive. These tests require careful frequency selection.

**The discrete-time period problem.** A sine at frequency `f₀` sampled at `f_OS` has a discrete period of `f_OS / gcd(f₀, f_OS)` samples. If `gcd(f₀, f_OS)` is small, this period is long, and the discrete-time nonlinearity produces harmonics at every multiple of `gcd(f₀, f_OS)` — not just at the odd multiples expected from a continuous-time analysis. These in-band harmonics cannot be filtered out; they are real spectral content.

The solution is to choose `f₀` such that `gcd(f₀, f_OS)` is large enough that no multiple of `gcd` falls in the test frequency range:

| f₀       | gcd(f₀, 192 kHz) | Multiples in [20, 22] kHz | Suitable? |
|----------|------------------|---------------------------|-----------|
| 5000     | 1000             | 20000, 21000, 22000       | No        |
| **6000** | **6000**         | **none**                  | **Yes**   |
| 7000     | 1000             | 20000, 21000, 22000       | No        |
| 8000     | 8000             | none                      | Yes       |

Additionally, choose the analysis window length N such that `f₀ × N / f_s` is an integer, ensuring exact DFT bin alignment and zero spectral leakage from the fundamental into the test range.

For the full post-mortem on how this was discovered see [lessons-learned.md § 13a](lessons-learned.md).

---

## Python Unit Tests (pytest)

`python/tests/test_effects.py` tests the Python prototypes independently of C++:

- Silence in → silence out (both effects)
- No NaN/Inf at parameter extremes
- Asymmetric clipping produces even-order harmonics
- Delay impulse appears at the correct sample offset
- Feedback decay matches the expected geometric series

These run with `uv run --project python pytest` and serve as a fast sanity check on the prototype before committing to a C++ port.

---

## Documentation and Plots

Each effect and shared component has a Markdown document in `docs/` covering its signal chain, parameters, design decisions, and filter coefficient lifecycle. Frequency-response and waveform plots are generated by `python/generate_docs_plots.py` and stored in `docs/img/`.

To regenerate all plots after an algorithm change:

```sh
uv run --project python python/generate_docs_plots.py
```

Plots are checked in alongside the Markdown so the documentation renders without a local Python environment.

---

## Adding a New Effect

1. Write a Python prototype in `python/<effect>.py` with a `process(signal, sr, **params)` function.
2. Add pytest unit tests in `python/tests/test_effects.py`.
3. Add golden cases to the `CASES` list in `python/generate_golden.py`; run `make golden`.
4. Implement the C++ effect in `libs/effects/<Effect>.h` extending `Effect`.
5. Add the effect to `tests/wav_compare.cpp` so `make validate` can drive it.
6. Write Catch2 behavioural tests in `tests/test_<effect>.cpp`.
7. Run `make compare` to verify C++ output matches Python golden within 5 × 10⁻⁴.
8. Write documentation in `docs/<Effect>.md` and add plots via `generate_docs_plots.py`.

### 9. Wire into the JUCE plugin

In `juce-plugin/PluginProcessor.h`, add a `<Effect> effect[2]` member (one per stereo channel).

In `juce-plugin/PluginProcessor.cpp`:

- `createParameterLayout()` — add one `AudioParameterFloat` or `AudioParameterBool` per knob using `juce::ParameterID{"<prefix>_<name>", 1}`. Use `withStringFromValueFunction` to format the text-box display (e.g. `"300 ms"`, `"75%"`). Use `juce::NormalisableRange` with a skew factor for logarithmic parameters such as time and frequency.
- `prepareToPlay()` — call `effect[i].prepare(sampleRate, samplesPerBlock)` for each channel.
- `processBlock()` — read each parameter via `*apvts.getRawParameterValue("prefix_name")`, call the effect setters, then `effect[i].process(buffer.getWritePointer(i), numSamples)`.
- `processBlockBypassed()` — copy input to output and call `effect[i].reset()`.

In `juce-plugin/PluginEditor.h`, add a `juce::Slider` + `juce::Label` + `SliderAttachment` (or `juce::ToggleButton` + `ButtonAttachment`) for each parameter.

In `juce-plugin/PluginEditor.cpp`:

- Constructor — initialise each `SliderAttachment` with `(p.apvts, "prefix_name", slider)`, configure the slider style, and call `addAndMakeVisible`.
- `paint()` — draw a section header in the effect's accent colour and a vertical or horizontal divider if sharing the window with other effects.
- `resized()` — lay out the knobs and labels using `setBounds`. Add a new section to the existing layout or expand the window height.

Pick an accent colour from the project palette (see README Color Scheme) and register it in both the SVG and the `paint()` literal.

### 10. Wire into the VCV Rack module

Create `vcv-rack/src/<Effect>Module.cpp`. Follow the pattern of `OverdriveModule.cpp` or `DelayModule.cpp`:

- Declare `ParamIds`, `InputIds`, `OutputIds`, `LightIds` enums.
- In the constructor, call `config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS)`, then `configParam` / `configSwitch` for each knob/toggle, `configInput` / `configOutput` for each port, and `configBypass(AUDIO_INPUT, AUDIO_OUTPUT)`.
- Override `onSampleRateChange()` to call `effect.prepare(e.sampleRate, 1)`.
- Override `onReset()` to call `effect.prepare(APP->engine->getSampleRate(), 1)`.
- In `process()`, read each param, apply CV offsets (±5 V → ±half the parameter range is the standard mapping), call the effect setters, normalise the input sample from ±5 V to ±1, call `effect.process(&sample, 1)`, and clamp the output back to ±12 V.

Create `vcv-rack/res/<Effect>.svg`. Use the `#1a1a2e` panel background, the effect's accent colour for the top/bottom strips, title text, and OUT jack label, and `#888` for IN and CV labels. Place knobs and jacks using `mm2px(Vec(x_mm, y_mm))` coordinates; a standard 8HP (40.64 mm) panel fits three large knobs in a column with two CV rows and audio I/O at the bottom.

In `vcv-rack/src/plugin.cpp` and `vcv-rack/plugin.json`, register the new model: add `extern Model* model<Effect>;` and `p->addModel(model<Effect>)`, and add the module entry to `plugin.json`.

Add `<Effect>Module.cpp` to the `SOURCES` list in `vcv-rack/CMakeLists.txt`.
