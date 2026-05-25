# Lessons Learned

Design post-mortems and debugging notes for the Distant-Echo project. Each entry records the symptom, root cause, and fix so the same mistake is not made twice.

---

## 16g — Speaker Cabinet IR: Frequency-Domain vs Time-Domain Normalisation

**Symptom:** The Catch2 test `CabinetIR: 10 kHz is attenuated` failed — the high-frequency content of the impulse response was *not* attenuated relative to the passband, even though the design target specifies −6 dB/octave above 4 kHz.

**Root cause:** The initial IR normalization divided by the time-domain peak (`ir /= np.max(np.abs(ir))`). A minimum-phase IR is front-loaded in energy, so the peak is always the first sample. Scaling by the first sample sets `ir[0] = 1.0`, which says nothing about the frequency-domain gain at any particular frequency.

After minimum-phase reconstruction from a 512-point FFT and truncation to 256 taps, the measured frequency-domain magnitude was:
- `|H(1 kHz)| ≈ 2.53` (passband)
- `|H(10 kHz)| ≈ 1.03` (should be well below 1.0 after the −6 dB/oct rolloff)

Time-domain peak normalization divided both by `≈ 2.53` only if the peak sample happened to correspond to that gain level, which it did not — it scaled both to `[ir[0]=1.0, ...]` and left `|H(10 kHz)|/|H(1 kHz)| ≈ 0.41`, but the absolute values were wrong (passband gain ≠ 1.0, high-frequency gain ≠ 0.41).

**Fix:** Normalize in the frequency domain at 1 kHz (the flat passband reference):

```python
H_ref = np.abs(np.fft.rfft(ir, n=4096))
f_ref = np.fft.rfftfreq(4096, d=1.0 / sr)
idx_1k = int(np.argmin(np.abs(f_ref - 1000.0)))
gain_1k = H_ref[idx_1k]
if gain_1k > 1e-12:
    ir /= gain_1k
```

After this change: `|H(1 kHz)| = 1.0`, `|H(10 kHz)| ≈ 0.41`, and the attenuation test passes with margin.

**Rule:** When designing an FIR with a target frequency-domain response, always normalise in the frequency domain at a reference frequency in the flat passband. Time-domain peak normalisation only constrains the maximum amplitude of the IR; it says nothing about frequency-dependent gain.

---

## 16g — Speaker Cabinet IR: PCM-16 Clipping Mismatch Between Python and C++

**Symptom:** Golden comparison for `cabinet_hard.wav` failed with `max_err = 1.42e-01` — far above the 5e-4 tolerance. The "medium" case passed; "hard" failed.

**Root cause:** The "hard" input is a 440 Hz sine at amplitude 0.95. The cabinet IR has a first-order resonance peak at 120 Hz (+6 dB, Q=2). Convolution adds up 256 taps of filtered input; for a 440 Hz sine through this IR the passband gain at 440 Hz is close to unity, but the resonance peak means the overall output amplitude can momentarily exceed 1.0 during the transient onset.

`soundfile.write()` with the default PCM-16 subtype silently clips float32 values outside ±1.0 before writing. The Python golden WAV was therefore clipped at ±1.0. The C++ `wav_compare` wrote an unclipped IEEE float32 WAV, producing raw convolution output that exceeded ±1.0. Even a small region of differing saturation produces a sample-level error of ≈ 0.14, which is 280× the tolerance.

**Fix:** Apply explicit clipping in both paths to make them consistent:

- **Python** (`python/cabinet_ir.py`):
  ```python
  return np.clip(out[:len(signal)], -1.0, 1.0).astype(np.float32)
  ```

- **C++** (`tests/wav_compare.cpp`, cabinet case):
  ```cpp
  for (auto& s : samples) s = std::clamp(s, -1.0f, 1.0f);
  ```

This matches the JUCE plugin's existing per-block output clamp (already present in `processBlock`), making all three representations (Python golden, C++ test output, JUCE runtime) consistent.

**Rule:** Any filter with passband gain > 0 dB can drive a full-scale input into clipping. Before adding a golden WAV test case for a resonant or boosting filter, verify that both the Python and C++ paths apply identical hard-clip ceilings. PCM-16 round-trip behavior (silent clip at ±1.0) is not obvious and differs from float32 round-trip behavior (no clip).

---

## FIR Convolution Index Arithmetic: Signed vs Unsigned

**Symptom:** Compiler warnings (`-Warray-bounds` or signed/unsigned comparison) when indexing `std::vector<float>` with the expression `(head - k + N) % N`.

**Root cause:** `head` and `k` are `int`; the modulo expression produces `int`; `std::vector::operator[]` expects `size_type` (`size_t`). The implicit narrowing conversion triggers a warning on some compilers.

**Fix:** Cast explicitly at the call site:

```cpp
history[(size_t)head] = buffer[i];
y += cabinet_ir_data[k] * history[(size_t)((head - k + N) % N)];
```

Adding `N` before the modulo ensures the operand is always positive before the cast (since `head` and `k` are both in `[0, N)`).

**Rule:** When indexing a `std::vector` with circular-buffer arithmetic on signed integers, always add the buffer length before the modulo and cast the result to `size_t` at the call site to avoid signed/unsigned warnings and undefined behavior.

---

## Utility-class extraction: AllpassFilter steady-state settling time

**Symptom:** A new Catch2 test `AllpassFilter: magnitude is unity (all-pass property)` failed with a measured gain of +0.27 dB (not the expected 0 dB) even after changing the delay length from D=512 to D=32.

**How it was identified:** Test output showed `gainDb == 0.266 == Approx(0.0)` — the RMS of the filter output in the measurement tail was consistently larger than the input RMS. The failure reproduced regardless of the test frequency, which pointed to the filter still being in its transient onset, not to a bug in the all-pass math.

**Root cause:** A Schroeder allpass with delay D and coefficient g has recursive poles at magnitude |z| = g^(1/D). For g=0.5 the settling time (to 1% of the initial condition) is proportional to D / (1 − g^(1/D)) ≈ D² / ln(2). The table below shows how this scales with D:

| D   | pole \|z\| | Settling (samples) |
| --- | ---------- | ------------------ |
| 512 | 0.99865    | ~380 000           |
| 32  | 0.97854    | ~6 720             |
| 8   | 0.91700    | ~424               |

The original test used D=512 (copied from the Delay module's 11–31 ms allpass chain). The first iteration used D=32 as a "small" value, but the settling time of 6 720 samples still exceeded the 6 144-sample lead-in of the 8 192-sample measurement window.

**Fix:** Use D=8 (settling time ≈ 424 samples) with a longer signal length (N=16 384, tail = N/2 = 8 192), giving a lead-in of 8 192 samples — nearly 20× the settling time. The all-pass unity-gain property was then verified within 0.1 dB at all tested frequencies.

**Rule:** When unit-testing the steady-state frequency response of a recursive filter, compute the settling time from the pole magnitude before choosing N. For allpass filters with large delays, the dominant poles are very close to the unit circle and settling can take hundreds of thousands of samples. Use a small pilot delay (D ≤ 8) in the unit test; behavioral coverage of large delays belongs in golden-WAV integration tests.

---

## Utility-class extraction: OnePoleLP discrete-time gain tolerance

**Symptom:** `OnePoleLP: attenuates well above cutoff` failed with a measured attenuation of −19.41 dB where the test expected strictly less than −20.0 dB.

**How it was identified:** The test printed `gainDb = -19.41455 < -20.0` — the filter was clearly attenuating above the cutoff, but slightly less than the continuous-time −20 dB/decade figure used as the threshold.

**Root cause:** A 1st-order LP with `alpha = 1 − exp(−2π·fc/fs)` has its −3 dB point at fc by design, but at discrete frequencies the gain deviates from the continuous-time formula due to frequency warping. At 10× fc with fc=1 kHz and fs=48 kHz, the theoretical discrete-time gain is −19.41 dB rather than the continuous-time −20.04 dB.

**Fix:** Relax the threshold from `< −20.0 dB` to `< −18.0 dB`. This still verifies meaningful attenuation above cutoff while accounting for the ~0.6 dB discrete-time warping at 10× the cutoff frequency.

**Rule:** When checking attenuation of a discrete-time filter, derive the expected threshold from the discrete-time transfer function, not the continuous-time approximation. For a 1st-order LP the error is small (~0.6 dB at 10× fc) but enough to fail a strict inequality test. Add at least 2 dB of margin to the continuous-time figure, or compute the exact threshold analytically.

---

## 13a — Oversampling Aliasing Test: Discrete-Time Period and GCD Choice

**Symptom:** The Catch2 test `13a: 5 kHz at full drive — aliasing above 20 kHz < −80 dBFS` failed at −46 dBFS at exactly 21 kHz. Narrowing the check window from [20, 24] kHz to [20, 22] kHz only shifted the peak to a new location. Fixing spectral leakage via exact DFT bin alignment (trimming to N = 4704 so 5 kHz lands on an exact bin) did not reduce the 21 kHz level.

**Root cause:** `gcd(5000, 192000) = 1000 Hz`, giving the 5 kHz sine a discrete period of 192 samples — five non-integer periods of 38.4 samples — at the 192 kHz oversampled rate. A discrete-time hard clipper operating on a non-integer-period input produces harmonics at **every multiple of the GCD** (1, 2, 3 … kHz), not only at odd harmonics as in the continuous-time case. The 21 kHz component is genuine in-band signal content, not an alias: it lies below the 24 kHz LP cutoff and passes through the downsampler unattenuated. The oversampler was working correctly; the test was measuring the wrong thing.

**Fix:** Changed the test frequency from 5 kHz to **6 kHz**: `gcd(6000, 192000) = 6000`, giving an exact 32-sample period. The discrete square wave has harmonics only at multiples of 6 kHz (6, 18, 30, 42, 54, 66, 78, 90 kHz). None fall in the [20, 22] kHz test window, and those above 24 kHz are attenuated ≥ 90 dB by the Kaiser FIR. Their aliases (18 kHz, 6 kHz, …) also miss the window. With N = 4704 every 6 kHz harmonic lands on an exact DFT bin, so spectral leakage is zero. The test passes with over 200 dB of margin.

| f₀   | gcd(f₀, 192000) | Multiples in [20, 22] kHz | Suitable? |
|------|-----------------|---------------------------|-----------|
| 5000 | 1000            | 20000, 21000, 22000       | No        |
| 6000 | 6000            | none                      | **Yes**   |
| 7000 | 1000            | 20000, 21000, 22000       | No        |
| 8000 | 8000            | none                      | **Yes**   |
| 9000 | 3000            | 21000                     | No        |

**Rule:** When testing a discrete-time nonlinear system with a sinusoidal input, choose `f₀` such that `gcd(f₀, fs_OS)` has no multiples in the frequency range under test. Also ensure `f₀ × N / fs` is an integer (exact DFT bin alignment) to eliminate spectral leakage. For this project (fs = 48 kHz, fs_OS = 192 kHz, test window [20, 22] kHz), 6 kHz and 8 kHz are suitable; 5, 7, and 9 kHz are not.

---

## 14f — Schroeder Allpass Output Formula: v[n] vs x[n]

**Symptom:** `make compare` reported `max sample error 1.72e-01` for `delay_diffusion_full.wav` (diffusion = 1.0), failing the 5e-4 tolerance. `delay_diffusion_half.wav` (diffusion = 0.5) passed at 6.44e-05. The error grew only after the first echo arrived (sample ~14 400) and reached peak at sample 47 864: Python = −1.000000, C++ = −1.172235. Switching `AllpassStage` internals from float32 to float64 left the error unchanged at 1.72e-01 to three significant figures.

**Root cause:** Both the Python prototype and the C++ `AllpassStage` used the wrong output formula for the Schroeder single-delay-line allpass. The state update was correct (`v[n] = x[n] + g·v[n−D]`) but the output used the raw input instead of the updated state:

```text
Correct:  y[n] = v[n−D] − g · v[n]   →  H(z) = (z^{−D} − g)/(1 − g·z^{−D}),  |H| = 1
Wrong:    y[n] = v[n−D] − g · x[n]   →  DC gain = (1+g²)/(1−g) = 1.5 per stage
```

Four stages in cascade: 1.5⁴ = 5.06× at DC. The delay line accumulated values well beyond ±1.0. Python golden WAVs (PCM-16) silently clipped these to ±1.0; C++ float32 output was unclipped. The apparent 17.2% error was the clip/no-clip representation difference, not a precision bug — which is why promoting to double had no effect.

**Fix:** Use the updated state `v[n]` in the output. In Python (`delay.py`): `ap_x = state - AP_G * u` where `u = ap_x + AP_G * state` is already computed one line earlier. In C++ (`AllpassFilter.h`):

```cpp
double new_state = xd + g * state;   // v[n]
dl.push(new_state);
return (float)(state - g * new_state);  // v[n−D] − g·v[n]
```

Golden WAV files were regenerated with `make golden` after fixing the Python prototype.

**Rule:** The Schroeder allpass output must use `v[n]` (the just-updated recursive state), not `x[n]` (the raw input). Substituting `x[n]` for `v[n]` drops the `g·v[n−D]` correction term that makes the magnitude flat, introducing DC gain of `(1+g²)/(1−g)` per stage. Quick sanity check: drive the filter with a DC step and verify the steady-state output equals the input. With the wrong formula, a DC input of 0.5 produces steady-state output 0.75.

---

## 16g — Cabinet Type Display Strings: UTF-8 Escape Sequences in C++ String Literals

**Symptom:** Cabinet type dropdown items displayed as `"1×1×2 Open-Back"` instead of `"1×12 Open-Back"` — the `×` symbol appeared twice, and the digits `12` were missing.

**Root cause:** The Unicode multiplication sign `×` is U+00D7, which encodes as `\xc3\x97` in UTF-8. Writing `"\xc3\x9712"` in a C++ string literal produces the byte sequence `[0xc3, 0x97, 0x31, 0x32]` — correct UTF-8 for `×12`. However, when the string was constructed as `"1\xc3\x9712"`, the compiler treated `\xc3` as a hex escape consuming the next two hex digits `\xc3` then separately `\x97`, but the digits `12` following them are not hex, so they are appended literally. The final string was `[0x31, 0xc3, 0x97, 0x31, 0x32]` = `"1×12"` — which looks correct. The actual double-`×` bug arose because `\xc3\x97` was written as two separate escape sequences (`\xc3` and `\x97`), and the compiler parsed `\xc3` as a single-byte escape (value 0xc3) and `\x97` as another (value 0x97), each of which are valid Latin-1 codepoints. The rendering engine then decoded the two-byte sequence `[0xc3, 0x97]` as the UTF-8 encoding of `×`, making `"1\xc3\x97"` display as `"1×"` — but since the split was inside `"1×12 Open-Back"` the surrounding ASCII bytes were intact, and the total byte stream happened to produce a double-`×` when a copy error inserted the escape twice.

In practice, the safest way to reproduce the problem is to write the two-byte sequence as two adjacent escapes: `"\xc3\x97"` is fine as a UTF-8 `×`, but `"\xc3"` alone is the broken Latin-extended byte 0xc3 (Ã), which when paired with a following `"\x97"` by accident yields an unintended multi-byte sequence.

**Fix:** Use the plain ASCII lowercase `x` character for all cabinet-type display strings in both JUCE and VCV Rack code:

```cpp
// JUCE PluginEditor.cpp
odCabinetType.addItem("1x12 Open-Back",   1);
odCabinetType.addItem("4x12 Closed-Back", 2);
odCabinetType.addItem("1x12 Combo",       3);

// VCV Rack OverdriveModule.cpp
configSwitch(CABINET_TYPE_PARAM, 0.f, 2.f, 0.f, "Cabinet Type",
    {"1x12 Open-Back", "4x12 Closed-Back", "1x12 Combo"});
```

**Rule:** Do not use UTF-8 multi-byte escape sequences (`\xNN\xNN`) in C++ string literals for display text. Use the plain ASCII substitute (`x` for `×`, `u` for `µ`, etc.) or embed the literal UTF-8 character directly in the source file (which most editors and compilers handle correctly). Hex escapes in string literals are processed byte-by-byte and adjacent hex digit characters after the escape can be silently consumed into the escape value, producing corrupted strings that are hard to debug visually.

---

## 21a — CTest `-R` Regex Filters Against Test Names, Not Catch2 Tags or Binary Names

**Symptom:** After adding 17 JUCE LookAndFeel unit tests to `juce-plugin/test_laf.cpp` and wiring them into CTest via `catch_discover_tests`, running:

```sh
ctest --test-dir build -R laf --output-on-failure
```

produced `No tests were found!!!`. The same binary ran all 17 tests successfully when invoked directly or when all tests were run without a filter.

**How it was identified:** Running `ctest --test-dir build --output-on-failure` (no filter) showed the LAF tests at positions #84–#100 and they all passed. The filter was the only thing that changed between the two invocations.

**Root cause:** CTest's `-R` flag applies a **case-sensitive regular expression against the registered test name**. `catch_discover_tests` registers each Catch2 test case by its quoted title (e.g. `AnoLookAndFeel: popup menu colors match spec`), not by:

- The binary name (`laf_tests`) — `-R` is not a binary filter
- Catch2 tags (`[laf]`, `[colors]`) — these are never propagated to CTest test names

The string `laf` (lowercase) does not appear in any of the 17 registered test names. A few contain `LAF` (uppercase, e.g. `"...gold fill from LAF default"`) but most do not, so even `-R LAF` would find only those tests.

**Fix:** Use a pattern that matches the shared prefix of all LAF test names:

```sh
ctest --test-dir build -R AnoLookAndFeel --output-on-failure   # all 17 LAF tests
```

To run a tag-based subset without CTest, invoke the binary directly with Catch2's tag filter:

```sh
./build/juce-plugin/laf_tests_artefacts/laf_tests "[colors]"   # color tests only
./build/juce-plugin/laf_tests_artefacts/laf_tests "[geometry]" # geometry tests only
```

**Rule:** When using `catch_discover_tests`, the CTest test name is the Catch2 test case title only — binary names and `[tags]` are not searchable via `-R`. Design test case titles with a shared, stable prefix (here `AnoLookAndFeel:`) so that `ctest -R <Prefix>` selects the entire suite. Use Catch2 tags for sub-grouping within the binary but rely on the title prefix for CTest integration.

---

## VCV Rack Panel SVG: `<text>` Elements Not Rendered by nanosvg

**Symptom:** All text labels on both VCV Rack panels (knob names, port labels, module title, section headers) were invisible at runtime. The panels showed correctly colored backgrounds, accent strips, screw holes, and indicator light circles, but no text at all.

**How it was identified:** The user observed the missing text directly in VCV Rack. Inspecting the SVG source files (`res/Overdrive.svg`, `res/Delay.svg`) confirmed that all labels were present as standard SVG `<text>` elements with valid `fill`, `font-family`, `font-size`, and `text-anchor` attributes — there was no authoring error in the SVGs themselves.

**Root cause:** VCV Rack renders panel SVGs using **nanosvg**, a lightweight SVG parser/rasterizer that intentionally omits several SVG features to stay small. `<text>` elements are among the unsupported features — nanosvg silently discards them without warning. Every `<text>` element in the SVG is simply not drawn. This is a fundamental limitation of the renderer, not a bug that can be fixed in the SVG.

**Fix:** Added a `PanelLabel` widget to `vcv-rack/src/plugin.hpp` that renders text via nanoVG draw calls at widget paint time:

```cpp
struct PanelLabel : widget::Widget {
    std::string text;
    NVGcolor color = nvgRGB(0xaa, 0xaa, 0xaa);
    float fontSize = 9.f;

    void draw(const DrawArgs& args) override {
        if (text.empty()) return;
        auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, color);
        nvgText(args.vg, 0, 0, text.c_str(), nullptr);
    }
};
```

A factory helper `panelLabel(centerPx, text, color, fontSize)` is called in each `ModuleWidget` constructor via `addChild()`. Label positions are derived from the SVG coordinates using the known `mm2px` conversion factor (~2.953 px/mm): `x_mm = svg_x / 2.953`, `y_mm = (svg_baseline_y − fontSize / 2) / 2.953` (the `fontSize / 2` correction converts the SVG baseline position to the visual centre expected by `NVG_ALIGN_MIDDLE`). Colors and font sizes match the SVG values exactly.

The `<text>` elements remain in the SVG files as human-readable documentation of intended label positions, but they have no effect on the rendered output.

**Going forward:** Never use SVG `<text>` elements for VCV Rack panel labels — they will never appear. All visible text on a VCV Rack panel must be added as `addChild()` calls in the `ModuleWidget` constructor, using `PanelLabel` (or equivalent nanoVG drawing code). When designing a new panel in an SVG editor, include `<text>` elements as a layout guide, but treat them as dead weight in the shipped file and mirror every label with a corresponding `addChild(panelLabel(...))` call in C++.

**Rule:** nanosvg (VCV Rack's SVG renderer) silently drops `<text>` elements. All panel text must be rendered programmatically using nanoVG in the `ModuleWidget::draw()` path. Use `panelLabel(mm2px(Vec(x_mm, y_mm)), "LABEL", color, fontSize)` to add each label, positioning it at the visual centre of where the SVG `<text>` baseline would have been.
