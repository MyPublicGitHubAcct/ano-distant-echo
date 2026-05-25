# Delay.h — Feedback Delay

## Purpose

`Delay` implements a single-tap feedback delay line that emulates the sound of tape echo and bucket-brigade device (BBD) analog delays. Each repeat is progressively darker than the last because the feedback signal passes through a low-pass filter before being recirculated — matching the high-frequency roll-off characteristic of magnetic tape and BBD charge transfer.

## Signal Chain

```text
input ─┬──────────────────────────────────────────────► (1−mix) ──┐
       │                                                           ├──► output
       │  DelayLine (max 2000 ms)                                  │
       │  ┌─────────────────────────────────────┐      mix ───────┤
       └─►│ dl.push(in[n] + fb·LP(wet))        │                 │
          │ wet = readLerp(d)   [digital]       │──► wet ────────►┘
          │   or readLagrange(d)[tape]          │
          └─────────────────────────────────────┘
                          ▲
                          │  LP (1-pole, fc ≈ 4 kHz)
```

Each output sample (steady state):

```text
wet[n]    = readInterp(activeTimeSamples)      // readLerp (digital) or readLagrange (tape; tapeSat=true)
lpState  += alpha × (wet[n] − lpState)         // 1-pole LP in feedback
dl.push(in[n] + feedback × lpState)            // write current sample + LP'd feedback
env[n]   += α_a/α_r × (|in[n]| − env[n])      // duck: 5 ms/500 ms envelope follower
duckGain  = (1 − duckDepth) if env[n] > thresh, else 1
out[n]    = (1 − mix) × in[n] + mix × duckGain × wet[n]
```

During the 10 ms crossfade that follows a time change, `wet[n]` is replaced by a linear blend of two taps:

```text
wet[n] = fadeOut · readInterp(activeTimeSamples)
       + fadeIn  · readInterp(pendingTimeSamples)    // readLerp or readLagrange per tapeSat
```

where `fadeOut` ramps from 1 → 0 and `fadeIn` from 0 → 1 over `crossfadeSamples` (≈ 480 samples at 48 kHz). When the counter reaches zero, `activeTimeSamples` is updated to `pendingTimeSamples`.

Where `readInterp(d)` expands to one of:

**Digital mode** (`tapeSat = false`) — `dl.readLerp(d)`:

```text
d_int    = floor(d)
frac     = d − d_int                           // fractional part [0, 1)
read(k)  = buf[(pos − k + bufLen) % bufLen]    // k steps back from write position
readLerp = (1−frac)·read(d_int) + frac·read(d_int+1)
```

**Tape mode** (`tapeSat = true`) — `dl.readLagrange(d)`, a 5-point 4th-order kernel:

```text
d_int  = floor(d)
t      = d − d_int                             // fractional part [0, 1)
h₀     =  t(t−1)(t−2)(t−3) / 24
h₁     = (t+1)(t−1)(t−2)(t−3) / −6
h₂     = (t+1)t(t−2)(t−3)   / 4
h₃     = (t+1)t(t−1)(t−3)   / −6
h₄     = (t+1)t(t−1)(t−2)   / 24
readLagrange = h₀·read(d_int−1) + h₁·read(d_int)   + h₂·read(d_int+1)
             + h₃·read(d_int+2) + h₄·read(d_int+3)
```

Nodes are at integer offsets {−1, 0, 1, 2, 3} relative to `floor(d)`. The kernel collapses to a simple read at integer delays (at `t = 0`, `h₁ = 1` and all others = 0). The extra read at `d_int+3` requires the buffer guard region to be 4 samples wider than for linear interpolation — `maxRead` is capped at `size − 5` (vs. `size − 2`) when tape mode is active.

## Parameters

| Parameter | Range | Default | Description |
| --- | --- | --- | --- |
| `time_ms` | 1–2000 ms | 300 ms | Delay time. Sub-sample accuracy via linear interpolation. |
| `feedback` | 0–0.95 (normal) / 0–1.02 (self-osc) | — | Fraction of delayed signal fed back. Clamped at 0.95 by default; raised to 1.02 (with tape sat) or 0.98 (without) when `selfOscillate=true`. |
| `mix` | 0–1 | — | Wet/dry ratio. 0 = fully dry, 1 = fully wet. |
| `wowRate` | 0–2 Hz | 0 | Wow LFO rate. 0 = disabled. |
| `wowDepthMs` | 0–10 ms | 0 | Peak delay deviation for the wow LFO. |
| `flutterRate` | 3–12 Hz | 8 Hz | Flutter LFO rate. Only active when `flutterDepthMs > 0`. |
| `flutterDepthMs` | 0–2 ms | 0 | Flutter peak delay deviation. 0 = disabled. |
| `tapeSat` | bool | false | Enable tape saturation stage in feedback path. |
| `tapeAge` | 0–1 | 0 | Tape age: darkens LP cutoff (4 kHz→1.5 kHz), raises saturation drive (2→5). |
| `duckThreshold` | dBFS | 0 | Ducking threshold, −30–0 dBFS. Wet attenuated when dry input env exceeds this. |
| `duckDepth` | 0–1 | 0 | Duck depth: 0 = no effect; 1 = wet fully muted while input exceeds threshold. |
| `diffusion` | 0–1 | 0 | Pre-delay diffusion: 0 = bypassed, 1 = full four-stage Schroeder allpass chain. |
| `selfOscillate` | bool | false | Unlocks feedback ceiling to 1.02 (tape sat on) or 0.98 (tape sat off). With tape sat enabled the tanh softclipper prevents runaway at unity-or-above feedback — the "infinite sustain / freeze" mode of premium delays. Without tape sat, 0.98 provides controlled build-up without the nonlinear limiting. |
| `pingPong` | bool | false | Stereo mode for `processStereo()`: false = independent L/R (R time = L×1.02); true = ping-pong (cross-channel feedback, echoes alternate channels). |

> In the JUCE and VCV Rack hosts, `wowRate`, `wowDepthMs`, `flutterRate`, and `flutterDepthMs` are exposed as a single **Mod Depth** knob (0–1) at fixed rates: wow at 0.5 Hz (0–4 ms depth) and flutter at 8 Hz (0–1 ms depth). The individual setters remain available in the DSP library API.
>
> In VCV Rack, three dedicated timing inputs share a panel row below the CV row (all at y = 111 mm), with priority **V/OCT > CLK > TAP > TIME knob**:
>
> - **TAP** — gate/trigger (rising edge ≥ 1 V). Two successive rising edges establish the tap interval (10–4000 ms), which overrides the TIME knob while the cable is connected. The first tap only primes the counter; no update occurs until the second tap. TIME CV applies as an offset on the tap-derived base.
> - **CLK** — clock gate (rising edge ≥ 1 V). Same rising-edge mechanism as TAP but intended for a master clock; two clock pulses set the delay time from the measured period. TIME CV applies as an offset.
> - **V/OCT** — pitch CV (1 V/oct, 0 V = A4 = 440 Hz). Maps to delay time via `time_ms = 1000 / (440 × 2^v)`, giving 2.27 ms at 0 V, 4.54 ms at −1 V, and so on. TIME CV offset is suppressed when V/OCT is connected to preserve pitch accuracy.

`feedback` and `mix` are smoothed through `SmoothedValue` (20 ms ramp) to prevent zipper noise on parameter changes. `duckThreshold` and `duckDepth` are not smoothed — the 5 ms attack / 500 ms release IIR envelope follower itself provides the smooth duck-engage and release behaviour.

## Tape Saturation

When `tapeSat = true`, the feedback signal is passed through a tanh softclipper before being written back into the delay buffer:

```text
fbSig[n] = tanh(satDrive · lpState) / tanh(satDrive)
buf[n]   = in[n] + feedback · fbSig[n]
```

`satDrive` defaults to 2.0, which provides light compression; the normalisation by `tanh(satDrive)` keeps unity gain for small signals. This limits runaway amplitude build-up at high feedback values and adds warmth to each successive repeat — a characteristic of overdriven tape heads.

`tapeAge` scales both the LP cutoff and saturation drive simultaneously, modelling the degradation of an aging tape machine:

| `tapeAge` | LP cutoff | `satDrive` |
|-----------|-----------|------------|
| 0         | 4000 Hz   | 2.0        |
| 0.5       | 2750 Hz   | 3.5        |
| 1         | 1500 Hz   | 5.0        |

At full age the repeats are significantly darker and more heavily saturated.

## Pre-Delay Diffusion

When `diffusion > 0`, the dry input passes through a cascade of four Schroeder allpass filters before being written into the main delay buffer. Each stage has the transfer function:

```text
H(z) = (z^{−D} − g) / (1 − g·z^{−D})       |H(e^{jω})| = 1  ∀ω
```

implemented with a single circular buffer per stage:

```text
v[n]  = x[n] + g · v[n−D]      (state — recursive with delay D)
y[n]  = v[n−D] − g · v[n]      (output — uses updated state, not raw x)
```

| Stage | Delay (ms) | Delay @ 48 kHz |
|-------|------------|----------------|
| 1     | 11 ms      | 528 samples    |
| 2     | 17 ms      | 816 samples    |
| 3     | 23 ms      | 1104 samples   |
| 4     | 31 ms      | 1488 samples   |

Coefficient g = 0.5 for all stages. The four delays are mutually prime to avoid periodic cancellations.

Because |H| = 1 at every frequency, the chain does not colour the spectrum. It does smear the phase response — transient energy that would arrive in a sharp burst at the delay tap is instead spread across a ~20 ms window, giving each repeat a diffuse "bloom" quality similar to a spring reverb or early reflections pattern. At `diffusion = 0` the chain is bypassed entirely and the signal goes straight to the delay buffer; at intermediate values a linear crossfade `(1−d)·x + d·allpass(x)` blends dry and diffused signal.

The `AllpassStage` helper struct in `Delay.h` uses `DelayLine<double>` internally so that state accumulation in the recursive path matches Python's float64 prototype within the 5e-4 tolerance.

## Feedback LP Filter

The feedback path uses a first-order IIR low-pass at approximately 4 kHz:

```text
alpha = 1 − exp(−2π × 4000 / sampleRate)
y[n]  = (1−alpha) × y[n−1]  +  alpha × x[n]
```

This is the impulse-invariance approximation to a first-order RC low-pass. The `−3 dB` point lands at approximately 4 kHz.

![Delay LP filter response](img/delay-lp-response.png)

The filter attenuates the high-frequency content of each repeat, making successive echoes progressively darker — as heard on tape delays and vintage BBD pedals.

## Echo Decay

![Echo decay envelope at various feedback values](img/delay-echo-decay.png)

At `feedback = 0.9` the echoes sustain for several seconds; at `feedback = 0.3` they die out after the first two repeats. The slope of the dB-per-repeat decay line depends on both the feedback level and the LP roll-off.

## Implementation Notes

### Interpolation: digital (linear) and tape (Lagrange)

`delaySamples` is stored as a `float`. In **digital mode** (`tapeSat = false`), `process()` calls `dl.readLerp(d)`, which splits `d` into an integer floor and a fractional remainder, reads two adjacent taps — `d` samples old and `d+1` samples old — and blends them with `(1−frac)·read(d) + frac·read(d+1)`. The buffer carries two extra guard slots so the `d+1` tap always points to a valid past sample.

In **tape mode** (`tapeSat = true`), `process()` instead calls `dl.readLagrange(d)`, a 5-point 4th-order polynomial interpolator. The kernel reads five adjacent taps at integer offsets {−1, 0, 1, 2, 3} relative to `floor(d)`. Because the outermost read lands at `d_int+3`, `maxRead` is capped at `size − 5` rather than `size − 2`.

Lagrange interpolation has a substantially flatter frequency response than linear out to higher fractions of Nyquist, reducing the high-frequency attenuation that linear interpolation introduces at fractional delays — particularly audible in the wow and flutter modulation paths where the delay time varies continuously. The coupling of Lagrange to `tapeSat` is intentional: digital BBD emulation uses linear interpolation (real BBD quantisation noise dominates over linear interpolation error), while tape mode benefits from the smoother fractional-delay response that Lagrange provides.

### Time-change crossfade

When `setTimeMs()` or `setTimeSamples()` is called while the effect is running, the difference between `activeTimeSamples` and the new value would cause the read pointer to jump — producing a pitch glitch or click on abrupt edits. To prevent this, `Delay` keeps two read positions: `activeTimeSamples` (the current source) and `pendingTimeSamples` (the new target). A `crossfadeCounter` counts down from `crossfadeSamples` (10 ms, computed in `prepare()`) to zero, and during that window `process()` blends the two taps linearly. When the counter reaches zero `activeTimeSamples` is updated.

If a second time change arrives while a crossfade is in progress, `pendingTimeSamples` is updated to the newest target and the counter continues without resetting (preserving the smooth fade envelope). If the new target matches `activeTimeSamples` the crossfade is cancelled immediately.

### Buffer pre-allocation

The circular buffer is allocated at `prepare()` time for the maximum possible delay (2000 ms + 2 samples). No per-sample allocation occurs.

### Feedback clamping and self-oscillation

`setFeedback()` clamps the incoming value to `maxFeedback()`, a private helper that returns:

- **0.95** — `selfOscillate = false` (default). Values above this cause unbounded build-up; the core processing loop has no additional limiting.
- **0.98** — `selfOscillate = true`, `tapeSat = false`. Provides controlled build-up without the tanh limiter; the margin below 1.0 prevents exponential runaway even without saturation.
- **1.02** — `selfOscillate = true`, `tapeSat = true`. The tanh softclipper in the feedback path limits amplitude growth — feedback at or slightly above unity sustains indefinitely rather than blowing up.

Call `setSelfOscillate(true)` before `setFeedback()` so the correct ceiling is applied immediately.

---

## Design Decisions

### Circular buffer

The delay line is implemented by `DelayLine<float>` (`libs/effects/DelayLine.h`) — a pre-allocated circular buffer with O(1) `push` and `read` per sample and no data movement. A FIFO-based implementation would require O(block_size) copies every `process()` call. `DelayLine` is shared with `Oversampler`, which also uses it for its FIR history buffers. The buffer is sized to the maximum allowed delay at `prepare()` time so no allocation ever occurs inside `process()`.

### 1-pole LP in the feedback path

A single-pole RC-equivalent low-pass is the physically correct model for HF loss in both magnetic tape (head-gap response, skin effect) and bucket-brigade devices (sample-and-hold reconstruction). A 2nd-order Butterworth would be steeper but would darken each repeat more aggressively than real analog media and wouldn't match the underlying physics of either device. The 1-pole design is simultaneously simpler and more accurate to the emulation target.

### Impulse-invariance LP coefficient

The formula `alpha = 1 − exp(−2π·fc/SR)` maps the analog RC time constant directly to the digital domain, preserving the exact decay rate at the design frequency. The bilinear transform alternative applies a `tan(π·fc/SR) / (π·fc/SR)` frequency warp that is negligible at 4 kHz vs. 48 kHz but adds unnecessary computation. Impulse-invariance is the standard choice for a 1-pole LP in a feedback path where time-constant accuracy matters more than stopband behavior.

### Feedback ceiling: 0.95, 0.98, or 1.02 depending on mode

At 0.95 (default), each repeat is attenuated by approximately 0.45 dB, giving well-controlled long tails without the risk of unbounded build-up. Clamping at exactly 1.0 would produce marginally-stable behavior where accumulated DC or rounding error causes the output to grow without bound.

Self-oscillation (`selfOscillate = true`) raises this ceiling in two tiers:

- **0.98 (tape sat off):** the 2% headroom below unity ensures the feedback converges even without a limiter, while still allowing dramatically long, building tails not possible at 0.95.
- **1.02 (tape sat on):** the tanh softclipper in the feedback path (`tanh(satDrive · lpState) / tanh(satDrive)`) bounds the recirculated amplitude — at unity-or-above feedback, the clipper saturates and holds the level steady rather than letting it grow. This reproduces the "infinite sustain / freeze" character of premium tape-echo units and boutique delay pedals.

### Wet output uses the unfiltered delay signal

The wet/dry blend uses `wet[n]` — the raw read from the delay buffer — not `lpState`, the LP-filtered feedback signal. The LP in the feedback path darkens *subsequent* repeats by filtering what is written back into the buffer. The direct wet signal should remain at full fidelity so that the first echo sounds as clean as the source, matching real tape and BBD hardware where HF roll-off accumulates across multiple passes through the medium rather than on the first playback.

### Parameter smoothing strategy

`feedback` and `mix` use `SmoothedValue` (1-pole exponential, 20 ms time constant) because any instantaneous jump in these gain scalars produces audible zipper noise. `time_ms` is not routed through `SmoothedValue`: a level smoother on the delay time would not prevent the read-pointer jump when the integer part of `delaySamples` changes. Instead, a 10 ms read-pointer crossfade (see "Time-change crossfade" above) handles abrupt time edits — the two mechanisms address orthogonal problems (gain zipper noise vs. position jump). Linear interpolation handles sub-sample fractional accuracy independently of both.

### One-pole exponential smoother (not linear ramp)

`SmoothedValue` uses `current = coeff·current + (1−coeff)·target`, a first-order IIR with `coeff = exp(−1/(SR·rampMs))`. This settles asymptotically — it never reaches the target exactly — but the perceptual ramp is indistinguishable from a 20 ms linear ramp for audio parameters. The exponential approach requires a single multiply-add per sample and a trivial coefficient formula; a linear ramp requires a step size recomputed on every target change and a separate termination check.

### Tape saturation: tanh placed after LP, not before

The tanh stage follows the 1-pole LP rather than preceding it. Placing saturation after the LP means only the LP-filtered (spectrally darkened) signal is softclipped — preserving the LP's role in shaping the tonal character of each repeat. If saturation came before the LP, the LP would smooth out the harmonic content introduced by the tanh, removing the "edge" of tape saturation. The chosen order matches the signal flow in a real tape head: tape magnetisation saturates after the signal has been band-limited by the head-gap response.

### `tanh(satDrive · x) / tanh(satDrive)` normalisation

The normalisation term keeps unity gain for small signals: as `x → 0`, `tanh(satDrive · x) → satDrive · x`, so `tanh(satDrive · x) / tanh(satDrive) → x`. This means that at low feedback and low input levels, tape saturation has no audible effect — it only colours the sound as levels rise. A simple `tanh(x)` without normalisation would attenuate quiet signals by `1 / tanh(satDrive)` (a DC gain less than 1), causing the first few quiet repeats to be softer than expected.

### Duck tracks dry input, not the delay line output

The envelope follower reads `abs(in[n])` — the dry sample before it is mixed into the delay line write — not the wet output. This correctly reflects the "duck delay while playing" use case: the instrument signal triggers the ducking, and when the player stops, the delays emerge. Tracking the wet output would create a feedback loop (duck reduces wet, wet falls, duck releases, wet rises, …) that could never settle.

### Hard threshold duck, not proportional gain reduction

When `env > thresh`, `duck_gain = 1 − duckDepth` (a fixed attenuation), rather than a gain that scales continuously with how far the envelope is above the threshold. A hard threshold matches the musical intent: while the player is playing, the delays are suppressed; when they stop, the delays emerge at full level. Proportional gain reduction would create a level-dependent blend that is harder to control and less predictable in practice. The envelope follower's smoothed attack and release make the transitions gradual even though the gain rule itself is binary.

### 5 ms attack / 500 ms release asymmetry

A fast attack (5 ms) ensures that the duck engages within one or two notes after the threshold is crossed, avoiding a brief "delay bleed" at the start of a phrase. A slow release (500 ms) gives the envelope time to settle well below the threshold between notes in a phrase — a short release would cause the duck to flicker on and off between notes at typical playing tempos. The 100:1 attack-to-release ratio is consistent with standard compressor design for this type of side-chain.

### Known limitations

- **Tape saturation is opt-in:** for BBD emulation (which has no tape saturation) `tapeSat` should remain false; the LP-only feedback path is the more accurate BBD model.
- **Duck depth is all-or-nothing per threshold:** partial ducking (`duckDepth < 1`) applies a fixed gain reduction rather than a soft knee. A soft-knee duck (ramping gain over a threshold range) is not implemented.
- **Self-oscillation without tape sat is not truly safe at exactly 1.0:** the 0.98 ceiling for the `selfOscillate=true, tapeSat=false` mode is a conservative guard; enabling self-oscillation without tape saturation is intended as a controlled build-up mode, not a true freeze.

## Ducking

When `duckDepth > 0`, a peak envelope follower tracks the absolute value of the dry input sample and attenuates the wet gain when the envelope exceeds `duckThreshold`:

```text
absx[n] = |in[n]|
env[n]  = env[n-1] + α_a · (absx[n] − env[n-1])   if absx[n] > env[n-1]  (attack)
        = env[n-1] + α_r · (absx[n] − env[n-1])   otherwise               (release)

duck_gain[n] = 1 − duckDepth   if env[n] > thresh_linear
             = 1               otherwise

out[n] = (1 − mix) · in[n] + mix · duck_gain[n] · wet[n]
```

where `thresh_linear = 10^(duckThreshold/20)` and the IIR coefficients are:

```text
α_a = 1 − exp(−1 / (SR × 0.005))   // 5 ms attack  (fast rise)
α_r = 1 − exp(−1 / (SR × 0.500))   // 500 ms release (slow fall)
```

The feedback path is not affected by ducking — the delay buffer continues to accumulate echoes while the wet output is suppressed. When the dry signal drops below the threshold and the envelope releases, the accumulated echoes re-emerge naturally.

## Stereo Processing

`Delay` overrides `Effect::processStereo()` to provide two true stereo modes, selected by `setPingPong()`.

### Stereo-independent mode (`pingPong = false`, default)

Two fully independent delay lines (`dl` for L, `dlR` for R) run in parallel. The right channel delay time is scaled by 1.02× relative to the left, creating a subtle stereo width effect without cross-channel interaction. All processing — LP filter, diffusion allpass chain, tape saturation, ducking, wow/flutter LFOs — runs independently per channel.

```text
dl.push(inL + fb·LP(wetL))     dlR.push(inR + fb·LP(wetR))
outL = (1−mix)·inL + mix·wetL  outR = (1−mix)·inR + mix·wetR
```

### Ping-pong mode (`pingPong = true`)

L and R delay lines exchange feedback: the LP-filtered output of the L delay is written back into the R delay line, and vice versa. This causes echoes to alternate between channels — the first repeat appears on R, the second on L, and so on.

```text
dl.push(inL + fb·LP(wetR))     dlR.push(inR + fb·LP(wetL))
outL = (1−mix)·inL + mix·wetL  outR = (1−mix)·inR + mix·wetR
```

Both modes share a single set of LFO state (wow/flutter phases) so modulation is coherent across channels. The crossfade counter is shared; the R channel derives its effective time from L's `activeTimeSamples` × stereoRatio each sample.

Calling `process()` (mono) ignores `pingPong` entirely — `dlR` and the R-channel filter states are not touched.

## API

```cpp
Delay d;
d.prepare(48000.0, 512);
d.setTimeMs(300.0, 48000.0);       // converts ms → fractional samples internally
d.setTimeSamples(14400.5f);        // set delay in samples directly (float)
d.setFeedback(0.5f);
d.setMix(0.5f);
d.setWowRate(0.5f);                // wow LFO rate in Hz (0 = disabled)
d.setWowDepthMs(2.0f);             // peak delay deviation for wow LFO, ms
d.setFlutterRate(8.0f);            // flutter LFO rate in Hz
d.setFlutterDepthMs(0.5f);         // peak delay deviation for flutter LFO, ms
d.setDiffusion(0.5f);              // 0 = dry, 1 = full four-stage allpass diffusion
d.setTapeSat(true);                // enable tape saturation + Lagrange interpolation
d.setTapeAge(0.5f);                // 0 = fresh tape, 1 = heavily aged
d.setDuckThreshold(-20.0f);        // duck threshold in dBFS (−30 to 0)
d.setDuckDepth(1.0f);              // 1 = fully mute wet while input is above threshold
d.setSelfOscillate(true);          // unlock feedback > 0.95 (call before setFeedback)
d.setFeedback(1.0f);               // clamped to 1.02 (tapeSat on) or 0.98 (tapeSat off)
d.setPingPong(true);               // ping-pong stereo (false = independent channels)
d.process(buffer, numSamples);     // mono in-place
d.processStereo(left, right, N);   // stereo in-place; mode set by setPingPong()
d.reset();                         // zero delay lines, LP states, and duck envelopes
```

## File Location

`libs/effects/Delay.h`

Prototype: `python/delay.py` — accepts `--interp linear` (default) or `--interp none`; `--tape-sat` auto-selects Lagrange; `--stereo` and `--ping-pong` flags exercise `process_stereo()`  
Golden validation: `tests/golden/delay_*.wav` (includes `delay_stereo_independent.wav` and `delay_stereo_pingpong.wav`)  
Catch2 tests: `tests/test_delay.cpp`
