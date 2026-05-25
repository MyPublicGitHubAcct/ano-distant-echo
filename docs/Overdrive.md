# Overdrive.h — Diode Hard-Clipping Overdrive

## Purpose

`Overdrive` emulates the signal path of a diode hard-clipping guitar overdrive pedal (DS-1 / RAT style). It provides five switchable waveshaper modes, 4× oversampling to suppress aliasing, pre/de-emphasis shelves that shape the harmonic character before and after clipping, a parametric mid EQ, a presence high shelf, and a pick-sensitivity envelope follower that reduces gain on loud transients to soften the attack character.

## Signal Chain

```
                        ┌── original rate (48 kHz) ─────────────────────────────┐
 in ──► Envelope     ──►│                                                        │
        follower (13e)  │ Upsample ×4  (13a)                                     │
                        │     │                                                  │
                        │     ▼  oversampled rate (192 kHz)                     │
                        │ DC block HP (100 Hz)                                   │
                        │     │                                                  │
                        │ ClipShape pre-emphasis shelf (13c)                     │
                        │     │                                                  │
                        │ Pre-amp gain ×(1–100) × envelope scalar (13e)         │
                        │     │                                                  │
                        │ Bias DC offset (16a)                                   │
                        │     │                                                  │
                        │ Waveshaper (13b)                                       │
                        │     │                                                  │
                        │ ClipShape de-emphasis shelf (13c)                      │
                        │     │                                                  │
                        │ Downsample ÷4 (13a)                                    │
                        │                                                        │
                        │ Tone LP/HP blend (3.5 kHz, 2nd-order Butterworth)     │
                        │     │                                                  │
                        │ Mid peaking EQ (800 Hz) (13d)                         │
                        │     │                                                  │
                        │ Presence high shelf (4 kHz) (13d)                     │
                        │     │                                                  │
                        │ Output level scalar                                    │
                        └────────────────────────────────────────────────────────┘
                                        │
                                       out
```

## Parameters

| Parameter   | Type           | Range        | Description |
|-------------|----------------|--------------|-------------|
| `drive`     | float          | 0–1          | Pre-amp gain, mapped to 1×–100× |
| `tone`      | float          | 0–1          | LP/HP blend at 3.5 kHz; 0 = dark, 1 = bright |
| `level`     | float          | 0–1          | Post-clip output level |
| `type`      | DistortionType | enum         | Waveshaper mode (see below) |
| `shape`     | ClipShape      | enum         | Pre/de-emphasis mode (Flat / MidFocus / BrightFocus) |
| `mid`       | float          | −6 to +10 dB | Peaking EQ at 800 Hz, 1.5-octave bandwidth |
| `presence`  | float          | 0 to +8 dB   | High shelf at 4 kHz |
| `pickSens`  | bool           | —            | Enable envelope-based gain reduction |
| `bias`      | float          | ±0.5         | DC offset applied immediately before the waveshaper (16a) |

`drive`, `tone`, `level`, `mid`, `presence`, and `bias` are all smoothed through `SmoothedValue` (20 ms ramp). `mid` and `presence` additionally trigger a biquad coefficient redesign (without state reset) each block when the smoothed value changes.

---

## Waveshaper Modes (DistortionType)

### Transfer Curves

![Waveshaper transfer curves](img/overdrive-waveshapers.png)

### HardClip

A Chebyshev polynomial knee blends smoothly from linear through the transition zone:

```
y = (3x − x³) / 2,  for |x| ≤ 1
y = ±1,              for |x| > 1
```

The polynomial exactly matches a hard clip at |x| = 1 (output = ±1) and reduces distortion in the transition zone relative to a pure threshold clipper. Produces odd harmonics only (symmetric transfer curve).

The slope at x = 0 is 3/2, giving a +3.52 dB small-signal gain relative to unity. This makes HardClip mode noticeably louder than SoftClip or Asymmetric modes at the same `drive` setting, particularly at low drive where signals stay well below the clip threshold and the polynomial knee region dominates.

### SoftClip

Normalised tanh saturation:

```
y = tanh(x) / tanh(g)
```

`x` is the signal after the pre-amp gain stage (`x = g × x_pre`). The denominator `tanh(g)` ensures unity output at the clip knee so the output level is independent of drive. The tanh curve has no hard knee — it saturates gradually, giving a warm, musical sound. Produces odd harmonics only.

### Foldback

Triangle-wave folding:

```
m = (x + T) mod 4T
y = T − |m − 2T|
```

The output folds back whenever the input exceeds ±T, creating a zig-zag mapping. This is not a smooth saturation curve — it produces a dense spectrum of both odd and even harmonics and creates ring-modulator-like tones at very high drive. Sounds harsh and inharmonic.

### Asymmetric

Silicon (+) / germanium (−) two-stage soft clipper:

```
y = tanh(x) / tanh(g)   for x ≥ 0   (harder — silicon diode model)
y = atan(x) × (2/π)     for x < 0   (softer — germanium diode model)
```

The different clipping characteristics for positive and negative half-cycles break the symmetry, producing strong even-order harmonics (2nd, 4th, …) in addition to the odd ones. This is the hallmark of germanium-diode distortion. The atan function has a wider knee than tanh, so the negative half saturates more softly.

### Bitcrush

Hard clip to ±1 followed by uniform mid-tread quantisation:

```
bits = 16 − round(drive × 14)     // 16-bit at drive=0, 2-bit at drive=1
step = 2 / 2^bits
y = round(x / step) × step
```

At drive = 0.5, the output is 9-bit. At full drive, 2-bit (four levels: ±0.5, ±1.0). The quantisation error is a fixed-pattern distortion spectrum whose structure depends on the ratio between the input frequency and the step size.

### Output Spectra

![Harmonic spectra for each mode](img/overdrive-spectra.png)

Input: 6 kHz sine, drive = 0.5, tone = 0.5. The dashed vertical line marks the 6 kHz fundamental.

- **HardClip** and **SoftClip**: odd harmonics only (6, 18, 30 … kHz); very clean
- **Foldback**: dense broadband spectrum extending to 24 kHz
- **Asymmetric**: even harmonics (12, 24 kHz) visible alongside odd ones
- **Bitcrush**: harmonic comb shaped by the quantisation step; note energy at subharmonics

---

## 4× Oversampling (13a)

The waveshaper runs at 192 kHz. The 128-tap Kaiser-windowed LP FIR attenuates all content above 24 kHz by more than 80 dB before downsampling, preventing high-frequency distortion harmonics from aliasing back into the audio band. See [Oversampler.md](Oversampler.md) for full details.

---

## ClipShape — Pre/De-Emphasis (13c)

Pre-emphasis boosts a frequency band before the clipper, causing those frequencies to saturate more heavily. De-emphasis applies the inverse filter after the clipper, restoring the flat frequency response. The net effect is a change in the harmonic character of the distortion — mid or high frequencies sound more overdriven than low frequencies — without changing the overall tonal balance.

| ClipShape    | Pre-emphasis shelf  | De-emphasis shelf |
|--------------|---------------------|-------------------|
| Flat         | none                | none              |
| MidFocus     | +6 dB HS @ 700 Hz   | −6 dB HS @ 700 Hz |
| BrightFocus  | +6 dB HS @ 3 kHz    | −6 dB HS @ 3 kHz  |

Both shelves are 2nd-order high-shelf biquads (Audio EQ Cookbook) designed at the oversampled rate (192 kHz).

![ClipShape pre/de-emphasis shelves](img/overdrive-clip-shapes.png)

---

## Tone Blend

The tone control blends two 2nd-order Butterworth filters sharing a 3.5 kHz crossover:

```
out = (1 − tone) × LP(x)  +  tone × HP(x)
```

At `tone = 0`, the output is purely low-passed (dark, no high-frequency content). At `tone = 1`, purely high-passed (bright, attenuated low end). At `tone = 0.5`, the blend is approximately flat across the mid-range.

![Tone blend frequency response](img/overdrive-tone-blend.png)

Note that the summed response is not flat for any `tone` value — the LP and HP filters do not form a perfect complementary pair. This intentional colouration is characteristic of the tone stacks in classic overdrive pedals.

---

## Mid EQ (13d)

A biquad peaking (parametric) EQ at 800 Hz with a 1.5-octave bandwidth:

![Mid peaking EQ](img/overdrive-mid-eq.png)

The 800 Hz centre frequency targets the presence region where the guitar's pick attack and body resonances are most audible. Boosting adds punch and crunch; cutting scoops out the midrange for a more scooped metal tone.

---

## Presence Shelf (13d)

A 2nd-order high shelf at 4 kHz, boosting only (0–8 dB):

![Presence high shelf](img/overdrive-presence-shelf.png)

The presence control adds air and cut to the distorted tone. Applied at the original sample rate, after downsampling, so its coefficients do not need re-computation at the oversampled rate.

---

## Envelope Follower — Pick Sensitivity (13e)

A peak-detector envelope follower tracks the raw input signal at the original rate:

```
coeff = envAttack  if |in| > state  else envRelease
state = coeff × state + (1 − coeff) × |in|
gainEnv = 1.0 − 0.292 × clamp(state, 0, 1)
```

| Parameter    | Time constant |
|--------------|---------------|
| Attack       | 1 ms          |
| Release      | 100 ms        |

![Pick sensitivity envelope follower response](img/overdrive-pick-sensitivity.png)

The gain multiplier ranges from 1.0 (no signal) down to 0.708 (−3 dB, at full-scale input). Loud pick attacks receive 1–3 dB less pre-amp gain than sustained notes, softening the bite of each pluck without compressing the sustain — matching the behaviour of a germanium transistor's gain roll-off at high input levels.

The envelope is computed on the raw input before upsampling, then upsampled by sample-and-hold (`np.repeat` in Python, integer-divided array index in C++) to apply it at the oversampled rate.

---

## DC Block

A first-order HP at 100 Hz runs at the oversampled rate (192 kHz), placed immediately after upsampling and before the gain stage. This removes any DC offset in the input before it reaches the clipper, where DC would cause asymmetric clipping that shifts the output baseline. The filter uses the bilinear-transform design:

```
wa = 2 × srOs × tan(π × 100 / srOs)
k  = 2 × srOs / (2 × srOs + wa)
p  = (2 × srOs − wa) / (2 × srOs + wa)
```

---

## Filter Coefficient Lifecycle

| Filter          | Rate    | Designed at    | Redesigned when               |
|-----------------|---------|----------------|-------------------------------|
| DC block HP     | OS rate | `prepare()`    | Only on `prepare()`           |
| ClipShape shelf | OS rate | `prepare()`    | Only on `prepare()`           |
| Tone LP/HP      | SR      | `prepare()`    | Only on `prepare()`           |
| Mid peaking EQ  | SR      | `prepare()`    | Also in `process()` when smoothed `mid` changes (no state reset) |
| Presence shelf  | SR      | `prepare()`    | Also in `process()` when smoothed `presence` changes (no state reset) |

`mid` and `presence` can change at runtime without calling `prepare()`, so their biquads are redesigned at block rate — once per `process()` call when the smoothed value differs from the value used in the previous call. The redesigned coefficients are held constant for the entire block (not updated per sample), trading per-sample control precision for one biquad design per block instead of `SR/blockSize` designs per second. No state reset is performed: the filter history carries over through the coefficient shift, allowing the biquad to settle naturally from the incremental change rather than introducing a transient.

---

## Design Decisions

### 4× oversampling factor

At 2× oversampling a distortion harmonic of an 18 kHz guitar tone reaches 36 kHz, and its alias folds back to 12 kHz — still audible, and hard to suppress with a gentle LP. At 4×, harmonics extend to 96 kHz (192 kHz OS rate); the 128-tap FIR attenuates everything above 24 kHz by ≥80 dB before downsampling, keeping all aliases below the noise floor. 8× would improve alias suppression further but quadruples CPU cost for gains that are inaudible in any real listening environment. 4× is the standard tradeoff in professional audio plugin design.

### Kaiser-windowed 128-tap FIR (not Hamming, Hann, or Parks-McClellan)

The Kaiser window with β = 8 achieves ~80 dB minimum stopband attenuation with 128 taps — sufficient to suppress aliases below −80 dBFS. Hamming gives ~40 dB and Hann ~44 dB; both are inadequate for transparent oversampling. A Parks-McClellan equiripple FIR would be marginally more efficient per tap, but designing one at runtime requires an iterative exchange algorithm with no closed-form solution. The Kaiser window is fully closed-form, computable in `prepare()` without iteration, and matches scipy's `firwin(128, 1/L, window=('kaiser', 8.0))` exactly — making Python-to-C++ golden validation accurate.

### Polyphase decomposition for the upsample path; direct decimation for the downsample path

The upsample path decomposes the 128-tap prototype FIR into 4 phases of 32 taps each. Each phase is applied once per input sample, computing one of the 4 oversampled output values. This is identical in operation count to the naive approach (4 × 32 = 128 multiply-adds per input sample) but accesses a 32-tap contiguous array rather than striding through 128 coefficients, improving cache behavior. The downsample path uses direct decimation — 128 multiply-adds on every 4th oversampled input sample — which is computationally symmetric per original-rate sample (128 mults / 4 inputs = 32 mults per input). Polyphase decomposition of the downsampler would restructure the same arithmetic without changing the operation count or cache footprint for a buffer-based API, so it is not applied there.

### Bilinear transform for the DC block HP

At the oversampled rate of 192 kHz, the bilinear transform's frequency warping factor at 100 Hz is `tan(π·100/192000) / (π·100/192000) ≈ 1.0000003`, making it indistinguishable from impulse-invariance at this frequency. The BT is used because it is the reference design in the Python prototype (scipy's bilinear transform) and its exact pre-warped coefficients match what `sosfilt` computes, keeping the golden comparison valid.

### DC block placed at oversampled rate, before the waveshaper

Any DC offset in the input — from pickup bias, cable capacitance, or ADC offset — causes asymmetric clipping: positive and negative half-cycles saturate at different thresholds, introducing a DC term and biasing the harmonic structure. Blocking DC before the gain × waveshaper eliminates this at the source. The HP could run at original rate before upsampling (upsampling preserves DC), but running it inside the oversampled section avoids coefficient redesign at a different sample rate and keeps the entire pre-clip signal path in one rate domain.

### ClipShape shelves designed at oversampled rate

The pre- and de-emphasis shelves are applied inside the oversampled signal path — upstream and downstream of the waveshaper — and must therefore be designed for 192 kHz. Designing them at 48 kHz and applying them before upsampling would misrepresent the shelf's effect on distortion harmonics that exist between the original and oversampled Nyquist frequencies.

### Tone, mid, and presence applied at original rate (not oversampled rate)

These are linear EQs applied after the nonlinear distortion and after downsampling. They do not interact with the waveshaper, so oversampling provides no benefit. Running them at 48 kHz reduces CPU by 4× relative to running them inside the oversampled loop.

### SmoothedValue for all continuously-varying parameters

All five continuously-varying parameters (`drive`, `tone`, `level`, `mid`, `presence`) use `SmoothedValue` (20 ms one-pole exponential ramp). For `drive`, `tone`, and `level`, the smoothed value feeds directly into the signal path each sample. For `mid` and `presence`, the smoothed value drives biquad coefficient redesign at block rate — once per `process()` call when the value has changed — without resetting filter state. Not resetting state lets the biquad settle naturally on incremental coefficient shifts, avoiding the one-sample glitch that a simultaneous coefficient change and state flush would produce.

### Waveshaper normalization choices

**SoftClip** uses `tanh(x) / tanh(g)` where `x = g · x_pre`. The `tanh(g)` denominator ensures the output reaches exactly ±1 when the pre-amplified input equals ±1, keeping output level independent of drive setting.

**HardClip** applies the Chebyshev polynomial `(3x − x³) / 2` *after* clamping the input to [−1, 1]. The polynomial gives a continuous first derivative at the clip boundary (unlike a pure threshold clipper), reducing high-order harmonic content from the sharp corner while still limiting hard at ±1.

**Asymmetric:** the positive half uses the same normalized tanh as SoftClip; the negative half uses `atan(x) · (2/π)`. Since `atan(±∞) = ±π/2`, the `2/π` factor normalizes the atan to ±1 at large input. The atan curve has a wider knee than tanh — the intentional softness of the germanium-diode negative half-cycle. Note that at moderate drive values the two halves have different small-signal gains (tanh scales by `1/tanh(g)`, atan by `2/π`), so the waveshaper introduces a mild DC bias in addition to the even-order harmonics.

### Envelope follower computed at original rate, applied via sample-and-hold at OS rate

The envelope tracks musical dynamics on the timescale of 1–100 ms — far slower than the 4-sample OS window (≈ 80 µs at 48 kHz). Computing the follower at original rate and repeating each value four times for the oversampled loop is an inaudible approximation that saves 75% of the follower's CPU cost relative to running it at the oversampled rate.

### Fast tanh approximation (`fastTanh`)

`SoftClip` and the positive half of `Asymmetric` mode call `tanh` per oversampled sample — at 4× oversampling that is 4 `tanh` evaluations per input sample. To avoid the cost of `std::tanh` (a double-precision transcendental), `Overdrive` uses a [7/6] rational Padé approximant:

```text
y = x × (135135 + x²×(17325 + x²×(378 + x²)))
       / (135135 + x²×(62370 + x²×(3150 + x²×28)))
```

![fastTanh accuracy: approximation vs exact tanh](img/overdrive-fasttanh.png)

Maximum error is less than 5×10⁻⁴ for |x| ≤ 5, clamped to ±1 beyond. This is a pure float multiply-and-divide sequence — roughly 3× faster than `std::tanh((double)x)`.

`scaleBuf` precomputes `fastTanh(g)` once per original-rate sample so the normalization denominator `tanh(g)` is available in the oversampled inner loop as a memory read rather than a repeated function call (which would otherwise run 4× per input sample at the oversampled rate).

The Asymmetric negative half still uses `std::atan` (float overload) — `atan` models the germanium diode curve and applies to only half the waveform, so its cost is lower and a polynomial approximation was not warranted.

### Bias placement in the signal chain (16a)

The `bias` offset is applied at the oversampled rate, after the pre-amp gain and envelope scalar, and immediately before the waveshaper. This placement means the bias interacts directly with the waveshaper's nonlinearity:

- On a symmetric waveshaper (`HardClip`, `SoftClip`), a non-zero bias shifts the operating point so the positive and negative half-cycles clip at different effective thresholds. The output waveform becomes asymmetric, introducing strong even-order harmonics (2nd, 4th, …) alongside the usual odd ones — simulating a transistor whose quiescent bias has drifted.
- The DC block HP at 100 Hz is applied *before* the bias, so the bias is not cancelled by it. The bias is a deliberate operating-point shift, not an unwanted DC offset.
- A `SmoothedValue` ramps the bias target over 20 ms to prevent zipper noise when the knob is turned.

### Known limitations

- **Asymmetric `atan` in negative half:** the negative half-cycle of `Asymmetric` mode calls `std::atan` (float overload). A polynomial approximation would reduce per-sample cost in this path, but `atan` is the deliberate model for the germanium diode characteristic and replaces the more expensive double-cast that was previously used.

---

## Speaker Cabinet IR

After the full signal chain, an optional `CabinetIR` stage can be applied. Three presets are available via the `od_cabinet_type` parameter:

| Preset | `od_cabinet_type` | Resonance | HF Rolloff |
| --- | --- | --- | --- |
| 1×12 Open-Back | 0 | +6 dB at 120 Hz | −6 dB/oct above 4 kHz |
| 4×12 Closed-Back | 1 | +6 dB at 80 Hz | −3 dB/oct above 3 kHz |
| 1×12 Combo | 2 | +6 dB at 180 Hz | −6 dB/oct above 5 kHz |

`CabinetIR::prepare()` resamples the selected 48 kHz base IR to the current host sample rate via linear interpolation, so resonance and rolloff frequencies remain correct at 44.1, 48, and 96 kHz. The IR is bypassed as a true no-op when `od_cabinet` is false.

See [CabinetIR.md](CabinetIR.md) for full IR design details, API reference, and JUCE/VCV Rack integration.

---

## Python Prototype

`python/overdrive.py` is the canonical reference implementation. All DSP decisions — filter formulae, parameter mapping, envelope follower coefficients — were prototyped there before being ported to C++. The acceptable per-sample error between the Python golden output and the C++ output is **5 × 10⁻⁴** (set by float32 biquad state vs float64 `sosfilt`; real algorithm bugs produce errors ≫ 10⁻³).

## File Location

`libs/effects/Overdrive.h`

Python prototype: `python/overdrive.py`  
Oversampler: `libs/effects/Oversampler.h` (see [Oversampler.md](Oversampler.md))  
Golden validation: `tests/golden/overdrive_*.wav`  
Catch2 tests: `tests/test_overdrive.cpp`
