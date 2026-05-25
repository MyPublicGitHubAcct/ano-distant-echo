#pragma once
// Minimal reader/writer for 32-bit IEEE-float WAV files (mono and stereo).
// Handles the files produced by Python soundfile.write() with float32 data.
// Multi-channel reads beyond stereo are mixed down to mono.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct WavData {
    std::vector<float> samples;
    int sampleRate = 0;
};

inline WavData readWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);

    auto r4 = [&]() -> uint32_t {
        uint32_t v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v;
    };
    auto r2 = [&]() -> uint16_t {
        uint16_t v = 0; f.read(reinterpret_cast<char*>(&v), 2); return v;
    };

    char id4[4];
    f.read(id4, 4);
    if (std::strncmp(id4, "RIFF", 4) != 0) throw std::runtime_error("not RIFF: " + path);
    r4();  // file size
    f.read(id4, 4);
    if (std::strncmp(id4, "WAVE", 4) != 0) throw std::runtime_error("not WAVE: " + path);

    uint16_t audioFmt = 0, channels = 0;
    uint32_t sr = 0;
    std::vector<float> samples;
    bool gotData = false;

    while (f) {
        char chunk[4]; f.read(chunk, 4);
        uint32_t size = r4();
        if (!f) break;

        if (std::strncmp(chunk, "fmt ", 4) == 0) {
            audioFmt = r2();
            channels = r2();
            sr       = r4();
            r4(); r2();             // byte-rate, block-align
            uint16_t bps = r2();
            (void)bps;
            if (size > 16) f.seekg(size - 16, std::ios::cur);

        } else if (std::strncmp(chunk, "data", 4) == 0) {
            if (audioFmt == 3) {   // IEEE float32
                uint32_t n = size / sizeof(float);
                samples.resize(n);
                f.read(reinterpret_cast<char*>(samples.data()), size);
                if (channels > 1) {
                    // Mix down to mono
                    uint32_t frames = n / channels;
                    std::vector<float> mono(frames);
                    for (uint32_t i = 0; i < frames; ++i) {
                        float sum = 0;
                        for (uint16_t c = 0; c < channels; ++c)
                            sum += samples[i * channels + c];
                        mono[i] = sum / channels;
                    }
                    samples = std::move(mono);
                }
            } else if (audioFmt == 1) {  // PCM 16-bit
                uint32_t frames = size / (channels * 2);
                samples.resize(frames);
                for (uint32_t i = 0; i < frames; ++i) {
                    float sum = 0;
                    for (uint16_t c = 0; c < channels; ++c) {
                        int16_t s = 0;
                        f.read(reinterpret_cast<char*>(&s), 2);
                        sum += s / 32768.0f;
                    }
                    samples[i] = sum / channels;
                }
            } else {
                throw std::runtime_error("unsupported WAV format (need float32 or PCM16)");
            }
            gotData = true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);  // skip + pad byte
        }
    }

    if (!gotData) throw std::runtime_error("no data chunk in: " + path);
    return WavData{std::move(samples), (int)sr};
}

struct WavDataStereo {
    std::vector<float> left;
    std::vector<float> right;
    int sampleRate = 0;
};

// Read a WAV file preserving stereo. Mono files duplicate the channel to left and right.
// Files with more than 2 channels are treated as stereo (first two channels used).
inline WavDataStereo readWavStereo(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);

    auto r4 = [&]() -> uint32_t {
        uint32_t v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v;
    };
    auto r2 = [&]() -> uint16_t {
        uint16_t v = 0; f.read(reinterpret_cast<char*>(&v), 2); return v;
    };

    char id4[4];
    f.read(id4, 4);
    if (std::strncmp(id4, "RIFF", 4) != 0) throw std::runtime_error("not RIFF: " + path);
    r4();
    f.read(id4, 4);
    if (std::strncmp(id4, "WAVE", 4) != 0) throw std::runtime_error("not WAVE: " + path);

    uint16_t audioFmt = 0, channels = 0;
    uint32_t sr = 0;
    std::vector<float> left, right;
    bool gotData = false;

    while (f) {
        char chunk[4]; f.read(chunk, 4);
        uint32_t size = r4();
        if (!f) break;

        if (std::strncmp(chunk, "fmt ", 4) == 0) {
            audioFmt = r2();
            channels = r2();
            sr       = r4();
            r4(); r2();
            r2();  // bps
            if (size > 16) f.seekg(size - 16, std::ios::cur);

        } else if (std::strncmp(chunk, "data", 4) == 0) {
            if (audioFmt == 3) {
                uint32_t n = size / sizeof(float);
                std::vector<float> raw(n);
                f.read(reinterpret_cast<char*>(raw.data()), size);
                uint32_t frames = (channels > 0) ? n / channels : n;
                left.resize(frames); right.resize(frames);
                for (uint32_t i = 0; i < frames; ++i) {
                    left[i]  = (channels >= 1) ? raw[i * channels + 0] : 0.f;
                    right[i] = (channels >= 2) ? raw[i * channels + 1] : left[i];
                }
            } else if (audioFmt == 1) {
                uint32_t frames = size / ((channels > 0 ? channels : 1) * 2);
                left.resize(frames); right.resize(frames);
                for (uint32_t i = 0; i < frames; ++i) {
                    int16_t l = 0, r16 = 0;
                    f.read(reinterpret_cast<char*>(&l), 2);
                    if (channels >= 2) f.read(reinterpret_cast<char*>(&r16), 2);
                    for (uint16_t c = 2; c < channels; ++c) { int16_t tmp; f.read(reinterpret_cast<char*>(&tmp), 2); }
                    left[i]  = l   / 32768.0f;
                    right[i] = (channels >= 2) ? r16 / 32768.0f : left[i];
                }
            } else {
                throw std::runtime_error("unsupported WAV format (need float32 or PCM16)");
            }
            gotData = true;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }

    if (!gotData) throw std::runtime_error("no data chunk in: " + path);
    return WavDataStereo{std::move(left), std::move(right), (int)sr};
}

// Write a stereo interleaved float32 WAV file.
inline void writeWavStereo(const std::string& path,
                            const std::vector<float>& left,
                            const std::vector<float>& right, int sr)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + path);

    uint32_t frames   = (uint32_t)std::min(left.size(), right.size());
    uint32_t dataSize = frames * 2 * (uint32_t)sizeof(float);
    uint32_t fileSize = 36 + dataSize;

    auto w4 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w2 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w4(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); w4(16);
    w2(3);                                          // IEEE float
    w2(2);                                          // stereo
    w4((uint32_t)sr);
    w4((uint32_t)sr * 2 * (uint32_t)sizeof(float)); // byte rate
    w2((uint16_t)(2 * sizeof(float)));               // block align
    w2(32);                                          // bits per sample
    f.write("data", 4); w4(dataSize);
    for (uint32_t i = 0; i < frames; ++i) {
        f.write(reinterpret_cast<const char*>(&left[i]),  sizeof(float));
        f.write(reinterpret_cast<const char*>(&right[i]), sizeof(float));
    }
}

inline void writeWav(const std::string& path, const std::vector<float>& samples, int sr) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write: " + path);

    uint32_t dataSize = (uint32_t)(samples.size() * sizeof(float));
    uint32_t fileSize = 36 + dataSize;

    auto w4 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w2 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w4(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); w4(16);
    w2(3);                                  // IEEE float
    w2(1);                                  // mono
    w4((uint32_t)sr);                       // sample rate
    w4((uint32_t)sr * sizeof(float));       // byte rate
    w2((uint16_t)sizeof(float));            // block align
    w2(32);                                 // bits per sample
    f.write("data", 4); w4(dataSize);
    f.write(reinterpret_cast<const char*>(samples.data()), dataSize);
}
