# Oversampler.h — 4× Polyphase FIR Oversampler

## Purpose

`Oversampler` upsamples an audio block by a factor of 4 (48 kHz → 192 kHz), allowing the nonlinear waveshaper inside `Overdrive` to operate at a higher sample rate, and then downsamples the result back to 48 kHz. The anti-aliasing low-pass filter applied during downsampling prevents high-frequency distortion harmonics from folding back (aliasing) into the audio band.

## Why Oversampling Is Needed

A hard clipper applied to a sine wave at 48 kHz generates odd harmonics extending well beyond the Nyquist frequency (24 kHz). These harmonics fold back — alias — into the audio band. For example, the 15th harmonic of a 5 kHz sine appears at 75 kHz, which aliases to 21 kHz after downsampling (since 96000 − 75000 = 21000). Without oversampling, this creates audible intermodulation distortion that does not exist in a true analog circuit.

At 4× (192 kHz), the anti-alias LP filter attenuates everything above 24 kHz before downsampling, so aliases land far below the noise floor.

## Filter Design

The LP FIR is designed at `prepare()` time to match scipy's `firwin(N, 1/L, window=('kaiser', 8.0))`:

- **128 taps** (symmetric Type-I → linear phase)
- **Kaiser window, β = 8.0** → minimum stopband attenuation ≈ −80 dB
- **Cutoff at 1/L of the upsampled Nyquist** = 24 kHz (the original Nyquist)
- **DC gain normalised to 0 dB**

### Frequency Response

![Oversampler FIR frequency response](img/oversampler-fir-response.png)

The passband extends to 24 kHz with < 0.1 dB ripple. The stopband begins at approximately 27.9 kHz and achieves > 80 dB attenuation throughout, reducing the dominant aliases (from the 5th harmonic of a 6 kHz signal at 30 kHz) by over 90 dB.

### Why Kaiser β = 8.0

Kaiser windows trade transition bandwidth for stopband attenuation. With β = 8.0 and 128 taps at a 4× oversampling rate:

- Transition band: 24 kHz → ≈ 27.9 kHz (≈ 4 kHz wide at 192 kHz rate)
- Minimum stopband attenuation: ≈ −80 dB (matching Kaiser's rule of thumb for β = 8)
- This is sufficient to push aliases at 30 kHz (5th harmonic of 6 kHz) below −90 dBFS

## Polyphase Upsample Structure

Upsampling by inserting L−1 zeros between each input sample and then filtering with a 128-tap FIR would require 128 multiplications per upsampled sample — or 512 per original sample. The polyphase decomposition reduces this to 32 multiplications per original sample.

The full filter is split into `L = 4` polyphase phases, each with `M = 32` taps:

```text
phases[k][m] = h[k + L*m],  k = 0…3, m = 0…31
```

For each input sample, the delay line at the original rate is advanced by one sample, and then `L` output samples are computed — one per phase, in order. Each phase performs a 32-tap dot-product with the shared delay line:

```cpp
upDL.push(in[n]);
for (int k = 0; k < L; ++k) {
    double acc = 0.0;
    for (int m = 0; m < M; ++m)
        acc += phases[k][m] * upDL.read(m + 1);
    out[n*L + k] = (float)(acc * L);   // ×L compensates zero-insertion gain loss
}
```

The `×L` gain correction restores unity DC gain after zero insertion (inserting L−1 zeros reduces the average signal level by L).

## Downsample Structure

Downsampling uses a direct 128-tap FIR applied to the full oversampled stream, decimating every L-th sample. The output at position `m` is computed immediately after inserting the input sample `in[L×m]`, matching scipy's `upfirdn(h, x, down=L)` decimation at indices 0, L, 2L, …:

```cpp
for (int i = 0; i < total && outIdx < numSamples; ++i) {
    dnDL.push(in[i]);
    if (i % L == 0) {
        double acc = 0.0;
        for (int m = 0; m < N_TAPS; ++m)
            acc += h[m] * dnDL.read(m + 1);   // m+1: read(1)=most recent → read(N_TAPS)=oldest
        out[outIdx++] = (float)acc;
    }
}
```

## Compile-Time Bypass

The oversampling factor is controlled by `OVERDRIVE_OVERSAMPLING` (default 4). Setting it to 1 at build time compiles out all FIR state and simply copies the input through:

```cpp
if constexpr (L == 1) {
    std::copy(in, in + numSamples, out);
    return;
}
```

This is useful for CPU profiling or testing alias artifacts without oversampling.

## Latency

The 128-tap FIR introduces a group delay of `(N_TAPS − 1) / 2 = 63.5` samples at the oversampled rate, or approximately 15.9 samples (≈ 0.33 ms at 48 kHz) at the original rate. This is a fixed, linear-phase delay — it does not cause frequency-dependent smearing.

## State

Each `Oversampler` instance has two independent `DelayLine<float>` instances (see `libs/effects/DelayLine.h`):

- **`upDL` (M = 32 samples)** — input samples at the original rate, for the polyphase upsample; tapped as `upDL.read(m + 1)` for m = 0…M−1
- **`dnDL` (N_TAPS = 128 samples)** — oversampled stream at 4× rate, for the downsample FIR; tapped as `dnDL.read(m + 1)` for m = 0…N_TAPS−1

`Overdrive` holds two separate instances: `osIn` for upsampling, `osOut` for downsampling. This keeps the two delay lines independent and avoids aliasing between them.

## API

```cpp
Oversampler os;
os.prepare(48000.0, blockSize);          // designs filter; allocates state
os.upsample(in,  osBuffer, numSamples); // numSamples → 4×numSamples
os.downsample(osBuffer, out, numSamples); // 4×numSamples → numSamples
os.reset();                              // zero state (no recompute)
```

## Further Reading

The interaction between the discrete-time period of the input signal and the oversampler's FIR filter is subtle — see [lessons-learned.md § 13a](lessons-learned.md) for a detailed account of a test design problem and its solution.

## File Location

`libs/effects/Oversampler.h`

Python reference: `python/oversampler.py`
