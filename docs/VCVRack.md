# VCV Rack Plugin — Distant-Echo

## Overview

Two modules sharing the `libs/effects/` DSP library. Each module processes one audio signal at a time (VCV Rack's per-sample engine), with CV inputs on all continuously-variable parameters and Eurorack-idiomatic features including tap tempo, clock sync, V/oct pitch, and gate bypass.

**Source:** `vcv-rack/src/OverdriveModule.cpp`, `vcv-rack/src/DelayModule.cpp`
**Panels:** `vcv-rack/res/Overdrive.svg`, `vcv-rack/res/Delay.svg`
**Plugin slug:** `Distant-Echo`

---

## Overdrive Module

10HP panel, gold accent `#c8a96e`.

### Overdrive Module - Panel Layout

```text
┌──────────────────┐
│    OVERDRIVE     │  ← gold title
│    ANOESIS       │
│                  │
│ [DRIVE]  [MID ]  │  large knob (x=15.24 mm) + small knob (x=35.56 mm), y=22 mm
│ [TONE ]  [PRES]  │  large knob + small knob, y=40 mm
│ [LEVEL]  [PICK]  │  large knob + CKSS toggle, y=58 mm
│          [BIAS]  │  small knob (x=35.56 mm), y=66 mm
│                  │
│ DRV TON LVL MID  │  CV row 1 (4 jacks), y=76 mm
│ PRS PCK BAS  ·   │  CV row 2 (y=89 mm); · = reserved for TYPE/SHAPE CV (TODO 20d)
│     [CAB]        │  Cabinet IR CKSS toggle (x=25.4 mm, y=96 mm)
│                  │
│ IN  BYP      OUT │  audio row, y=112 mm
│              ●●  │  SIGNAL/CLIP lights, y≈101–105 mm
└──────────────────┘
```

### Overdrive Module - Parameters

| Label | Param ID | Type | Range | Default | Description |
| --- | --- | --- | --- | --- | --- |
| DRIVE | `DRIVE_PARAM` | Float | 0–1 | 0.5 | Pre-amp gain into waveshaper |
| TONE | `TONE_PARAM` | Float | 0–1 | 0.5 | LP/HP blend at 3.5 kHz; 0=dark, 1=bright |
| LEVEL | `LEVEL_PARAM` | Float | 0–1 | 0.8 | Post-clip output level |
| MID | `MID_PARAM` | Float | −6 to +10 dB | 0 | Peaking EQ at 800 Hz |
| PRES | `PRESENCE_PARAM` | Float | 0 to +8 dB | 0 | High shelf at 4 kHz |
| PICK | `PICK_PARAM` | Switch | Off / On | On | Pick-sensitivity envelope gain reduction |
| BIAS | `BIAS_PARAM` | Float | ±0.5 | 0 | DC offset before waveshaper; shifts clipping asymmetry |
| TYPE | `TYPE_PARAM` | Switch | 0–4 | 0 | Distortion type — set via right-click menu |
| SHAPE | `SHAPE_PARAM` | Switch | 0–2 | 0 | Clip shape — set via right-click menu |
| CAB | `CABINET_PARAM` | Switch | Off / On | Off | Cabinet IR enable — panel CKSS toggle at x=25.4 mm, y=96 mm |
| — | `CABINET_TYPE_PARAM` | Switch | 0–2 | 0 | Cabinet preset — set via right-click menu: 0=1×12 open-back, 1=4×12 closed-back, 2=1×12 combo |

**Distortion types (TYPE):** Hard Clip, Soft Clip, Foldback, Asymmetric, Bitcrush

**Clip shapes (SHAPE):** Flat (no pre/de-emphasis), Mid Focus (+6 dB shelf above 700 Hz before clip), Bright Focus (+6 dB shelf above 3 kHz before clip)

### Overdrive Module - Inputs

| Jack | Label | Description |
| --- | --- | --- |
| `AUDIO_INPUT` | IN | Audio input (±5 V) |
| `DRIVE_CV_INPUT` | DRV | Drive CV: ±5 V → ±0.5 offset |
| `TONE_CV_INPUT` | TN | Tone CV: ±5 V → ±0.5 offset |
| `LEVEL_CV_INPUT` | LVL | Level CV: ±5 V → ±0.5 offset |
| `MID_CV_INPUT` | MID | Mid CV: ±5 V → ±8 dB offset |
| `PICK_CV_INPUT` | PCK | Pick Sens CV: gate > 1 V overrides panel toggle |
| `PRESENCE_CV_INPUT` | PRS | Presence CV: ±5 V → ±4 dB offset |
| `BIAS_CV_INPUT` | BAS | Bias CV: ±5 V → ±0.5 offset |
| `BYPASS_INPUT` | BYP | Bypass gate: > 1 V passes audio dry and resets effect state |

### Overdrive Module - Output

| Jack | Label | Description |
| --- | --- | --- |
| `AUDIO_OUTPUT` | OUT | Processed audio (±5 V nominal, clamped to ±12 V) |

### Overdrive Module - Indicator Lights

Both lights are positioned near the OUT jack (y≈101–105 mm):

| Light | Color | Condition |
| --- | --- | --- |
| `SIGNAL_LIGHT` | Green | Output magnitude > 0.05 V |
| `CLIP_LIGHT` | Red | Output magnitude > 4.75 V (95% of ±5 V rail) |

Lights use `setSmoothBrightness` for flicker-free response. Both are cleared to zero while bypassed or when the output jack is unconnected.

### Overdrive Module - Right-Click Context Menu

**Distortion Type** — checkmark list: Hard Clip / Soft Clip / Foldback / Asymmetric / Bitcrush. Writes directly to `TYPE_PARAM`.

**Clip Shape** — checkmark list: Flat / Mid Focus / Bright Focus. Writes directly to `SHAPE_PARAM`.

**Cabinet Type** — checkmark list: 1×12 Open-Back / 4×12 Closed-Back / 1×12 Combo. Writes directly to `CABINET_TYPE_PARAM`. Enable/disable cabinet IR via the `CAB` panel toggle.

### Overdrive Module - Bypass Behavior

- **Gate bypass (BYP jack):** while gate voltage > 1 V, the input is passed directly to the output and `effect.reset()` is called on the first rising edge. This zeroes all filter history so the effect re-engages cleanly when the gate falls.
- **Right-click → Bypass:** handled by `configBypass(AUDIO_INPUT, AUDIO_OUTPUT)`. VCV Rack routes input directly to output with no DSP involvement.
- **Right-click → Initialize:** calls `onReset()`, which calls `effect.prepare()` at the current sample rate — zeroes filter history and the oversampler state.

---

## Delay Module

10HP panel, blue accent `#6e9ac8`.

### Delay Module - Panel Layout

```text
┌──────────────────┐
│     DELAY        │  ← blue title
│    ANOESIS       │
│                  │
│    [TIME  ]      │  large knob, centred at x=25.4 mm, y=22 mm
│    [FDBK  ]      │  large knob, y=44 mm
│    [MIX   ]      │  large knob, y=66 mm
│                  │
│ [MOD]    [DIFF]  │  small knobs, x=10.16/40.64 mm, y=76 mm
│ [SAT]    [AGE ]  │  CKSS toggle + small knob, y=83 mm
│ [THRS][OSC][DPTH]│  duck knobs + self osc toggle, y=93 mm
│                  │
│ TIM  FBK MIX     │  CV row, y=104 mm
│ TAP  CLK V/O     │  timing row, y=111 mm
│                  │
│ LIN  BYP  RIN LOUT│  audio row, y=118 mm; R IN at x=30.48 mm
│               ●● │  R OUT at x=40.64 mm, y=123 mm; SIGNAL/CLIP lights
└──────────────────┘
```

### Delay Module - Parameters

| Label | Param ID | Type | Range | Default | Description |
| --- | --- | --- | --- | --- | --- |
| TIME | `TIME_PARAM` | Float | 1–2000 ms | 300 | Delay time (overridden by TAP/CLK/V/OCT when connected) |
| FDBK | `FEEDBACK_PARAM` | Float | 0–1.02 | 0.4 | Feedback ratio; values above 0.95 only take effect when OSC is on |
| MIX | `MIX_PARAM` | Float | 0–1 | 0.5 | Wet/dry blend |
| MOD | `MOD_DEPTH_PARAM` | Float | 0–1 | 0 | Wow (0.5 Hz, 0–4 ms) + flutter (8 Hz, 0–1 ms) depth |
| DIFF | `DIFFUSION_PARAM` | Float | 0–1 | 0 | Pre-delay allpass diffusion (four stages: 11/17/23/31 ms) |
| SAT | `TAPE_SAT_PARAM` | Switch | Off / On | Off | Tape saturation in feedback path; auto-selects Lagrange interpolation |
| AGE | `TAPE_AGE_PARAM` | Float | 0–1 | 0 | Darkens feedback LP (4 kHz → 1.5 kHz) and raises saturation drive |
| THRS | `DUCK_THRESHOLD_PARAM` | Float | −30 to 0 dB | 0 | Duck threshold; at 0 dB ducking is disabled |
| OSC | `SELF_OSC_PARAM` | Switch | Off / On | Off | Self-oscillation: unlocks feedback to 1.02 (tape sat on) or 0.98 (off) |
| DPTH | `DUCK_DEPTH_PARAM` | Float | 0–1 | 0 | Duck depth; 1 = wet fully muted while dry exceeds threshold |

### Delay Module - Inputs

| Jack | Label | Description |
| --- | --- | --- |
| `AUDIO_INPUT` | L IN | Left (or mono) audio input (±5 V) |
| `R_AUDIO_INPUT` | R IN | Right audio input (±5 V). When connected, enables stereo processing via `processStereo()`. |
| `TIME_CV_INPUT` | TIM | Time CV: ±5 V → ±1000 ms offset (suppressed when V/OCT connected) |
| `FEEDBACK_CV_INPUT` | FBK | Feedback CV: ±5 V → ±0.51 offset (clamped to param max) |
| `MIX_CV_INPUT` | MIX | Mix CV: ±5 V → ±0.5 offset |
| `TAP_INPUT` | TAP | Tap tempo gate: rising-edge interval sets delay time (10–4000 ms) |
| `CLK_INPUT` | CLK | Clock gate: rising-edge interval sets delay time (10–4000 ms) |
| `PITCH_INPUT` | V/O | V/oct pitch: `time_ms = 1000 / (440 × 2^v)` ms |
| `BYPASS_INPUT` | BYP | Bypass gate: > 1 V passes audio dry and resets effect state |

### Delay Module - Outputs

| Jack | Label | Description |
| --- | --- | --- |
| `AUDIO_OUTPUT` | L OUT | Left (or mono) processed audio (±5 V nominal, clamped to ±12 V) |
| `R_AUDIO_OUTPUT` | R OUT | Right processed audio. Active only when R IN is connected; outputs 0 V otherwise. |

### Delay Module - Stereo Processing

When R IN is connected, the module calls `effect.processStereo(&sampleL, &sampleR, 1)` each sample. The stereo mode is fixed to **independent channels** (R delay time = L × 1.02) — ping-pong mode is not exposed as a panel control in VCV Rack. When R IN is disconnected, the module falls back to mono `effect.process()` and R OUT outputs 0 V.

Both L and R pairs are registered with `configBypass` so right-click → Bypass passes both channels through dry.

### Delay Module - Indicator Lights

Both lights are positioned near the L OUT jack (y≈109.5–112 mm):

| Light | Color | Condition |
| --- | --- | --- |
| `SIGNAL_LIGHT` | Green | Output magnitude > 0.05 V |
| `CLIP_LIGHT` | Red | Output magnitude > 4.75 V (95% of ±5 V rail) |

### Delay Module - Timing Input Priority

When multiple timing sources are connected, the module applies the following priority on each sample:

```text
V/OCT > CLK > TAP > TIME knob
```

The TIME CV offset is applied on top of CLK and TAP, but is suppressed when V/OCT is connected — adding a CV offset would detune the pitch-locked frequency.

**TAP and CLK** both use rising-edge detection (≥ 1 V threshold). The first pulse primes the counter; the second pulse establishes the interval. Intervals outside 10–4000 ms are ignored. State is cleared when the jack is disconnected.

**V/OCT** maps linearly in pitch: `f = 440 × 2^v` Hz, `t = 1000 / f` ms. 0 V gives 2.27 ms (440 Hz resonance), −1 V gives 4.53 ms (220 Hz), +1 V gives 1.14 ms (880 Hz).

### Delay Module - Right-Click Context Menu

**Interpolation** — read-only label showing the current interpolation mode: `"Lagrange (tape)"` when Tape Saturation is on, `"Linear (digital)"` otherwise.

**Reset delay buffer** — calls `effect.reset()` without touching knob positions. Useful for clearing a built-up feedback tail without reinitializing the module.

### Delay Module - Bypass Behavior

- **Gate bypass (BYP jack):** while gate voltage > 1 V, both L and R inputs are passed directly to their respective outputs and `effect.reset()` is called on the first rising edge. Clears the entire delay buffer so the effect re-engages silently.
- **Right-click → Bypass:** handled by `configBypass` for both L and R pairs. VCV Rack routes inputs directly to outputs with no DSP involvement.
- **Right-click → Initialize:** calls `onReset()`, which calls `effect.prepare()` at the current sample rate — reallocates the delay buffer and zeroes all state.

---

## Building and Installing

```sh
# Build the plugin dylib
cmake -B build -G Ninja
cmake --build build --target Distant-EchoRack

# Install to VCV Rack 2 plugin directory
cmake --build build --target install-rack-plugin
# → ~/Library/Application Support/Rack2/plugins-mac-arm64/Distant-Echo/

# Package as distributable archive
cmake --build build --target dist-rack-plugin   # requires zstd
```

The VCV Rack SDK is expected at `~/Rack-SDK`. Override with `-DRACK_SDK_DIR=/path/to/sdk`.

Restart VCV Rack after installing.

---

## Known Gaps

| Item | Status |
| --- | --- |
| Distortion Type and Clip Shape CV inputs on Overdrive | TODO 20d |
| Custom accent-colored knob SVGs | TODO 20j |
| Delay ping-pong mode not exposed as a panel control | TODO 20m |
| Mod Depth and Diffusion have no CV jacks on Delay module | TODO 20n |
