# SmoothedValue.h — One-Pole Parameter Smoother

## Purpose

`SmoothedValue` prevents audible zipper noise when knob or CV values change abruptly between audio blocks. Every continuously-varying parameter in the effects library (`drive`, `tone`, `level`, `feedback`, `mix`) is smoothed through a `SmoothedValue` instance before it touches the signal path.

## Algorithm

The smoother is a single-pole IIR low-pass filter applied to the parameter value:

```
current[n] = coeff × current[n-1] + (1 − coeff) × target
```

The pole coefficient is chosen to give a time constant of `rampMs` milliseconds:

```
coeff = exp(−1 / (sampleRate × rampMs × 0.001))
```

This is the one-pole analogue of a first-order RC low-pass filter with time constant τ = `rampMs` ms. The filter settles to 63% of a step change in one time constant and to within 1% in approximately 4.6 × `rampMs`.

## Step Response

![SmoothedValue step response](img/smoothed-value-step.png)

At the default `rampMs = 20 ms` and 48 kHz sample rate, the pole is at:

```
coeff = exp(−1 / 960) ≈ 0.998959
```

The 63% point is at exactly 20 ms. At 80 ms (4 × τ) the output is within 2% of the target, and zipper noise is inaudible well before that.

## API

```cpp
SmoothedValue s;
s.prepare(sampleRate, 20.0);   // rampMs = 20 ms (default)
s.setTarget(newValue);         // set new destination — smoothing begins immediately
float v = s.next();            // advance one sample; call once per sample in process()
float v = s.get();             // read current value without advancing
s.reset(value);                // snap to value immediately (no ramp)
bool b = s.isSmoothing();      // true if |current − target| > 1e-6
```

`reset(value)` sets **both** `current` and `target` to the given value, so `isSmoothing()` returns `false` immediately after and `next()` returns `value` on every call until `setTarget()` is called again. The correct pattern is to call `reset(initialValue)` once before audio starts (so the first `next()` returns the right value, not 0), and then use `setTarget()` for all runtime changes. Calling `reset()` mid-stream does not cause the smoother to "resume from the snapped position" — it fully overwrites both poles of the filter state.

```cpp
```

## Usage in Effects

Each parameter that could change between audio blocks gets its own `SmoothedValue`:

```cpp
// Delay.h
SmoothedValue feedback, mix;

// Inside process():
for (int i = 0; i < numSamples; ++i) {
    float fb = feedback.next();   // smoothed per sample
    float mx = mix.next();
    // ... use fb and mx in the signal path
}
```

Calling `next()` once per sample is important. Calling it only once per block and holding the value constant would create a step at the block boundary — exactly the zipper noise being avoided.

## File Location

`libs/effects/SmoothedValue.h`
