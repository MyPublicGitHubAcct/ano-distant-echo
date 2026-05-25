# JUCE Plugin — Distant-Echo

## Overview

Distant-Echo is a dual-effect guitar processing plugin built on the shared DSP library in `libs/effects/`. It runs Overdrive and Delay in series — Overdrive first, Delay second — on every active channel. Supports VST3, AU (macOS), and Standalone formats.

**Source:** `juce-plugin/PluginProcessor.cpp`, `juce-plugin/PluginEditor.cpp`

---

## Signal Chain

```text
 audio in (mono or stereo)
    │
    ▼
 Overdrive  (per channel — skipped and reset when od_bypass is true)
    │
    ▼
 CabinetIR  (per channel — applied when od_cabinet is true; preset selected by od_cabinet_type)
    │
    ▼
 Delay      (stereo: delay[0].processStereo(L, R) — skipped and reset when dl_bypass is true)
            (mono:   delay[0].process(L))
    │
    ▼
 Clamp to [-1.0, 1.0]
    │
    ▼
 audio out
```

The processor maintains two `Overdrive` instances (one per channel) and two `Delay` instances; in stereo mode only `delay[0]` is used — it calls `processStereo()` to handle both channels with true cross-channel ping-pong feedback. `delay[1]` is prepared but unused in stereo mode (harmless). Each effect can be independently bypassed via its `od_bypass` / `dl_bypass` parameter — when active the effect's `reset()` is called every block to keep filter and delay history zeroed so there is no bleed-through on re-engagement. True bypass via host engage/disengage calls `processBlockBypassed()`, which copies input to output and calls `reset()` on all effect instances.

---

## Editor Layout

Fixed 600×400 window split vertically at x=300. Left half is the Overdrive section (gold header `#c8a96e`); right half is the Delay section (blue header `#4477aa`). A 1.5 px line divides the sections.

```text
┌────────────────────────────────┬────────────────────────────────┐
│    OVERDRIVE      [Bypass ☐]   │      DELAY        [Bypass ☐]   │
│─────────────────────────────── │ ─────────────────────────────  │
│  [Type ▾]  [Clip Shape ▾]      │  Time  Feedback  Mix           │
│  [Cabinet Type ▾]              │ ──────────────────────────     │
│  Drive Tone Level Mid Pres Bias│  Mod   Diff  Age  DkThr  DkDp  │
│                                │                                │
│  [Pick Sens ☑] [Cabinet IR ☐]  │  [Tape Saturation ☑] [Self Osc ☐]  │
│                                │  [Ping-Pong ☐]                     │
└────────────────────────────────┴────────────────────────────────┘
```

All knobs use `RotaryVerticalDrag` style with a text box below. Combo boxes and toggle buttons are standard JUCE components with no custom LookAndFeel applied (planned in TODO 21a).

---

## Parameters

All parameters are registered in `AudioProcessorValueTreeState` and fully automatable by the host. Version tag `1` on all parameter IDs.

All float parameters carry `withStringFromValueFunction` / `withValueFromStringFunction` lambdas: 0–1 knobs display as percentages, dB-range knobs as `±X.X dB`, and delay time as `XXX ms`. Typed values parse back correctly via the matching `withValueFromStringFunction` lambda.

### Overdrive

| APVTS ID | Display Name | Type | Range | Default | Display format | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `od_type` | OD Type | Choice | 0–4 | 0 | — | Hard Clip / Soft Clip / Foldback / Asymmetric / Bitcrush |
| `od_shape` | Clip Shape | Choice | 0–2 | 0 | — | Flat / Mid Focus / Bright Focus |
| `od_drive` | OD Drive | Float | 0–1 | 0.5 | `"50%"` | Pre-amp gain into waveshaper |
| `od_tone` | OD Tone | Float | 0–1 | 0.5 | `"50%"` | LP/HP blend at 3.5 kHz |
| `od_level` | OD Level | Float | 0–1 | 0.8 | `"80%"` | Post-clip output level |
| `od_mid` | OD Mid | Float | −6 to +10 dB | 0 | `"+0.0 dB"` | Peaking EQ at 800 Hz |
| `od_presence` | OD Presence | Float | 0 to +8 dB | 0 | `"+0.0 dB"` | High shelf at 4 kHz |
| `od_bias` | OD Bias | Float | ±0.5 | 0 | `"+0.00"` / `"-0.00"` | DC offset before waveshaper; generates even-order harmonics from symmetric clippers |
| `od_pick_sens` | Pick Sensitivity | Bool | — | true | — | Envelope-based transient gain reduction |
| `od_cabinet` | Cabinet IR | Bool | — | false | — | Enable cabinet IR convolution after the overdrive signal chain |
| `od_cabinet_type` | Cabinet Type | Choice | 0–2 | 0 | — | Cabinet preset: 0 = 1×12 open-back, 1 = 4×12 closed-back, 2 = 1×12 combo |
| `od_bypass` | OD Bypass | Bool | — | false | — | Bypass overdrive; resets effect state each block while active |

### Delay

| APVTS ID | Display Name | Type | Range | Default | Display format | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `dl_time` | Delay Time | Float | 1–2000 ms | 300 | `"300 ms"` | Skewed range (`skew=0.4`) for finer short-delay control |
| `dl_feedback` | Feedback | Float | 0–1.02 | 0.4 | `"40%"` | Feedback ratio; values above 0.95 only take effect when `dl_self_oscillate` is on |
| `dl_mix` | Delay Mix | Float | 0–1 | 0.5 | `"50%"` | Wet/dry blend |
| `dl_mod_depth` | Mod Depth | Float | 0–1 | 0 | `"0%"` | Wow (0.5 Hz, 0–4 ms) + flutter (8 Hz, 0–1 ms) at fixed rates |
| `dl_diffusion` | Diffusion | Float | 0–1 | 0 | `"0%"` | Four-stage Schroeder allpass pre-diffusion |
| `dl_tape_sat` | Tape Saturation | Bool | — | false | — | Tape saturation in feedback path; auto-selects Lagrange interpolation |
| `dl_tape_age` | Tape Age | Float | 0–1 | 0 | `"0%"` | Darkens LP cutoff (4 kHz → 1.5 kHz) and increases saturation drive |
| `dl_duck_threshold` | Duck Threshold | Float | −30 to 0 dB | 0 | `"-12 dB"` | At 0 dB, ducking is effectively disabled |
| `dl_duck_depth` | Duck Depth | Float | 0–1 | 0 | `"0%"` | 1 = wet fully muted while dry input exceeds threshold |
| `dl_self_oscillate` | Self Oscillate | Bool | — | false | — | Unlocks feedback > 0.95: 1.02 with tape sat (tanh limits runaway), 0.98 without |
| `dl_ping_pong` | Ping-Pong | Bool | — | false | — | Ping-pong stereo mode (echoes alternate L/R); independent stereo when off (R time = L × 1.02) |
| `dl_bypass` | Delay Bypass | Bool | — | false | — | Bypass delay; resets effect state each block while active |

---

## Audio I/O

- **Bus layout:** mono or stereo in/out; in and out channel count must match.
- **Sample rate / block size:** no restrictions; both effects call `prepare()` in `prepareToPlay()`.
- **Output clamping:** each sample is clamped to `[-1.0, 1.0]` after the full signal chain.
- **Bypass:** `processBlockBypassed()` calls `reset()` on all effect instances and delegates to `juce::AudioProcessor::processBlockBypassed()` for the copy. This ensures filter and delay history is clean when the effect re-engages.

---

## State Save / Restore

State is serialized through the APVTS tree as XML (`getStateInformation` / `setStateInformation`). Pluginval exercises this round-trip at every sample rate and block size — all passes confirmed.

---

## Building

```sh
cmake -B build -G Ninja -DBUILD_JUCE_PLUGIN=ON
cmake --build build --target Distant-Echo_VST3
cmake --build build --target Distant-Echo_Standalone
```

Install to macOS system directories:

```sh
cmake --build build --target install-vst3        # ~/Library/Audio/Plug-Ins/VST3/
cmake --build build --target install-au          # ~/Library/Audio/Plug-Ins/Components/
cmake --build build --target install-standalone  # ~/Applications/
cmake --build build --target install-juce        # all three
```

---

## Validation

```sh
# VST3 contract validation — strictness level 5
pluginval --strictness-level 5 --validate-in-process \
    build/juce-plugin/Distant-Echo_artefacts/VST3/Distant-Echo.vst3

# AU validation — required before loading in Logic Pro or GarageBand
auval -v aufx Anoe Anoe
```

---

## Known Gaps

| Item | Status |
| --- | --- |
| Custom LookAndFeel (colored arcs, dark knobs) | TODO 21a |
| Resizable editor window | TODO 21b |
| Per-effect bypass buttons | Done (TODO 21c) |
| Clip / activity indicator lights in editor | TODO 21d |
| Formatted value text for all knobs | Done (TODO 21f) |
| Tooltips on all controls | TODO 21g |
| Factory preset bank | TODO 21h |
| Delay/Overdrive routing order toggle | TODO 21i |
| Section accent border and visual polish | TODO 21j |
| Delay blue: JUCE `#4477aa` vs SVG `#6e9ac8` | TODO 21j |
