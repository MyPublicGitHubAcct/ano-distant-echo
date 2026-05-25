#pragma once
#include <algorithm>
#include <vector>

// Circular buffer for delay lines and FIR history buffers.
//
// Convention
// ----------
// push(x)       – write x at the current position and advance the write pointer.
// read(d)       – return the sample pushed d steps ago (d=1 = most recently pushed).
// readLerp(d)   – fractional tap via linear interpolation between read(floor(d))
//                 and read(floor(d)+1).
//
// Typical per-sample usage (delay of exactly D samples):
//
//   float wet = dl.readLerp(D);   // read BEFORE push
//   dl.push(inputSample);
//
// After push(x), read(1) == x.  Calling read(d) before the push in the same
// loop iteration is equivalent to reading d steps into the past.

template<typename T = float>
class DelayLine {
public:
    void resize(int n) {
        buf.assign((size_t)n, T(0));
        pos = 0;
    }

    void reset() {
        std::fill(buf.begin(), buf.end(), T(0));
        pos = 0;
    }

    int size() const { return (int)buf.size(); }

    // Write x and advance the write pointer.
    void push(T x) {
        buf[(size_t)pos] = x;
        pos = (pos + 1) % (int)buf.size();
    }

    // Read d steps back: d=1 is the most recently pushed sample.
    T read(int d) const {
        int n = (int)buf.size();
        return buf[(size_t)(((pos - d) % n + n) % n)];
    }

    // Fractional tap: linear interpolation between read(floor(d)) and read(floor(d)+1).
    T readLerp(float d) const {
        int di = (int)d;
        float frac = d - (float)di;
        return (1.0f - frac) * read(di) + frac * read(di + 1);
    }

    // Fractional tap: 4th-order Lagrange interpolation (5-point kernel).
    // Nodes at integer offsets {-1, 0, 1, 2, 3} relative to floor(d).
    // Requires d >= 1 and floor(d)+3 < size() — caller must clamp d before calling.
    T readLagrange(float d) const {
        int di = (int)d;
        float t = d - (float)di;
        float h0 =  t * (t-1.f) * (t-2.f) * (t-3.f) / 24.f;
        float h1 = (t+1.f) * (t-1.f) * (t-2.f) * (t-3.f) / -6.f;
        float h2 = (t+1.f) * t * (t-2.f) * (t-3.f) / 4.f;
        float h3 = (t+1.f) * t * (t-1.f) * (t-3.f) / -6.f;
        float h4 = (t+1.f) * t * (t-1.f) * (t-2.f) / 24.f;
        return h0 * read(di-1) + h1 * read(di)   + h2 * read(di+1)
             + h3 * read(di+2) + h4 * read(di+3);
    }

private:
    std::vector<T> buf;
    int pos = 0;
};
