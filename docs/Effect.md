# Effect.h — Abstract Effect Interface

## Purpose

`Effect.h` defines the pure-virtual base class that every DSP effect in the shared library implements. It establishes the minimal interface contract required by both the JUCE plugin host and the VCV Rack module wrapper.

## Interface

```cpp
class Effect {
public:
    virtual ~Effect() = default;
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(float* buffer, int numSamples) = 0;
    virtual void processStereo(float* left, float* right, int numSamples) {
        process(left, numSamples);
        std::copy(left, left + numSamples, right);
    }
    virtual void reset() {}
};
```

### `prepare(sampleRate, blockSize)`

Called once before audio streaming begins — and again whenever the host changes the sample rate or block size. Implementations use this to:

- Compute filter coefficients that depend on the sample rate
- Allocate and zero-initialise internal delay lines and filter states
- Resize scratch buffers to `blockSize`

Calling `prepare()` again effectively resets all internal state, since implementations zero their state as part of coefficient re-computation.

### `process(buffer, numSamples)`

Processes `numSamples` samples of audio in-place. The caller owns `buffer`; the effect reads from it and writes the result back to the same array.

The JUCE `AudioProcessor` calls this once per audio block. The VCV Rack module calls it per-sample (with `numSamples = 1`), since Rack's process loop is sample-by-sample.

### `processStereo(left, right, numSamples)`

Processes `numSamples` samples of stereo audio in-place. `left` and `right` are separate caller-owned arrays. The default implementation calls `process(left, numSamples)` and copies the result to `right` — a mono-to-stereo passthrough. Effects that implement true stereo processing (such as `Delay` with ping-pong or independent channel modes) override this.

The JUCE `AudioProcessor` calls `processStereo()` when two channels are active. The VCV Rack Delay module calls it per-sample (`numSamples = 1`) when the R IN jack is connected.

### `reset()`

Zeroes all filter states and delay-line contents without touching coefficients or sample-rate-dependent design. Provided with a default no-op so subclasses that do not maintain persistent state need not override it.

## Usage Pattern

```cpp
#include "Overdrive.h"

Overdrive od;                       // construction — no allocation yet
od.setDrive(0.7f);                  // set parameters any time
od.prepare(48000.0, 512);           // allocates; computes coefficients
od.process(audioBlock, 512);        // call each audio callback
```

The separation of `prepare()` from construction means effects can be declared as value members of plugin/module structs without triggering allocation at load time.

## File Location

`libs/effects/Effect.h`

This header must remain free of JUCE and VCV Rack includes so the library links cleanly into both plugin targets.
