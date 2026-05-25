// Command-line tool: read an input WAV, run a C++ effect, write output WAV.
// Used to produce tests/output/ files that compare.py diffs against tests/golden/.
//
// Usage:
//   wav_compare overdrive <in.wav> <out.wav> --drive 0.5 --tone 0.5 --level 0.8
//   wav_compare delay     <in.wav> <out.wav> --time-ms 300 --feedback 0.4 --mix 0.5
//   wav_compare delay     <in.wav> <out.wav> --time-ms 300 --feedback 0.4 --mix 0.5 --stereo
//   wav_compare delay     <in.wav> <out.wav> --time-ms 300 --feedback 0.4 --mix 0.5 --ping-pong

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "CabinetIR.h"
#include "Delay.h"
#include "Overdrive.h"
#include "wav_io.h"

static float getFloat(int argc, char* argv[], const char* flag, float def) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], flag) == 0)
            return std::stof(argv[i + 1]);
    return def;
}

static std::string getString(int argc, char* argv[], const char* flag,
                             const char* def) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], flag) == 0)
            return argv[i + 1];
    return def;
}

static DistortionType parseDistortionType(const std::string& s) {
    if (s == "softclip")   return DistortionType::SoftClip;
    if (s == "foldback")   return DistortionType::Foldback;
    if (s == "asymmetric") return DistortionType::Asymmetric;
    if (s == "bitcrush")   return DistortionType::Bitcrush;
    return DistortionType::HardClip;
}

static ClipShape parseClipShape(const std::string& s) {
    if (s == "midfocus")    return ClipShape::MidFocus;
    if (s == "brightfocus") return ClipShape::BrightFocus;
    return ClipShape::Flat;
}

static CabinetType parseCabinetType(const std::string& s) {
    if (s == "4x12")  return CabinetType::ClosedBack4x12;
    if (s == "combo") return CabinetType::Combo1x12;
    return CabinetType::OpenBack1x12;  // default: "1x12"
}

static bool hasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr <<
            "Usage: wav_compare <effect> <input.wav> <output.wav> [params]\n"
            "  wav_compare overdrive in.wav out.wav --drive 0.5 --tone 0.5 --level 0.8\n"
            "  wav_compare delay     in.wav out.wav --time-ms 300 --feedback 0.4 --mix 0.5\n"
            "  wav_compare delay     in.wav out.wav --time-ms 300 --stereo    (independent stereo)\n"
            "  wav_compare delay     in.wav out.wav --time-ms 300 --ping-pong (ping-pong stereo)\n"
            "  wav_compare cabinet   in.wav out.wav\n";
        return 1;
    }

    std::string effect     = argv[1];
    std::string inputPath  = argv[2];
    std::string outputPath = argv[3];

    try {
        WavData wav = readWav(inputPath);
        std::vector<float>& samples = wav.samples;
        int sr = wav.sampleRate;

        std::filesystem::create_directories(
            std::filesystem::path(outputPath).parent_path());

        if (effect == "overdrive") {
            float drive    = getFloat(argc, argv, "--drive",    0.5f);
            float tone     = getFloat(argc, argv, "--tone",     0.5f);
            float level    = getFloat(argc, argv, "--level",    0.8f);
            float mid      = getFloat(argc, argv, "--mid",      0.0f);
            float presence = getFloat(argc, argv, "--presence", 0.0f);
            float bias     = getFloat(argc, argv, "--bias",     0.0f); // 16a
            auto  mode     = parseDistortionType(getString(argc, argv, "--mode",  "hardclip"));
            auto  shape    = parseClipShape(getString(argc, argv, "--shape", "flat"));

            Overdrive od;
            od.setDrive(drive);
            od.setTone(tone);
            od.setLevel(level);
            od.setMid(mid);
            od.setPresence(presence);
            od.setBias(bias); // 16a
            od.setDistortionType(mode);
            od.setClipShape(shape);
            od.prepare((double)sr, (int)samples.size());
            od.process(samples.data(), (int)samples.size());

        } else if (effect == "delay") {
            float timeMs       = getFloat(argc, argv, "--time-ms",      300.0f);
            float feedback     = getFloat(argc, argv, "--feedback",       0.4f);
            float mix          = getFloat(argc, argv, "--mix",            0.5f);
            float wowRate      = getFloat(argc, argv, "--wow-rate",       0.0f);
            float wowDepth     = getFloat(argc, argv, "--wow-depth",      0.0f);
            float flutterRate  = getFloat(argc, argv, "--flutter-rate",   8.0f);
            float flutterDepth = getFloat(argc, argv, "--flutter-depth",  0.0f);
            float tapeAge      = getFloat(argc, argv, "--tape-age",       0.0f);
            float duckThreshold = getFloat(argc, argv, "--duck-threshold", 0.0f);
            float duckDepth     = getFloat(argc, argv, "--duck-depth",     0.0f);
            float diffusion     = getFloat(argc, argv, "--diffusion",      0.0f);
            bool  tapeSat   = hasFlag(argc, argv, "--tape-sat");
            bool  stereo    = hasFlag(argc, argv, "--stereo");
            bool  pingPong  = hasFlag(argc, argv, "--ping-pong");

            Delay dl;
            dl.setTimeMs((double)timeMs, (double)sr);
            dl.setFeedback(feedback);
            dl.setMix(mix);
            dl.setWowRate(wowRate);
            dl.setWowDepthMs(wowDepth);
            dl.setFlutterRate(flutterRate);
            dl.setFlutterDepthMs(flutterDepth);
            dl.setTapeSat(tapeSat);
            dl.setTapeAge(tapeAge);
            dl.setDuckThreshold(duckThreshold);
            dl.setDuckDepth(duckDepth);
            dl.setDiffusion(diffusion);
            dl.prepare((double)sr, (int)samples.size());

            if (stereo || pingPong) {
                // Stereo: treat mono input as L; R = same signal for independent, 0 for ping-pong
                dl.setPingPong(pingPong);
                std::vector<float> left = samples;
                std::vector<float> right = pingPong
                    ? std::vector<float>(samples.size(), 0.0f)
                    : samples;  // independent: identical input, output will differ by 1.02× time
                dl.processStereo(left.data(), right.data(), (int)samples.size());
                writeWavStereo(outputPath, left, right, sr);
                std::cout << "Written: " << outputPath << "\n";
                return 0;
            }

            dl.process(samples.data(), (int)samples.size());

        } else if (effect == "cabinet") {
            // 16g: direct-form FIR cabinet IR convolution + clip (matches JUCE processBlock clamp)
            auto cabType = parseCabinetType(getString(argc, argv, "--type", "1x12"));
            CabinetIR cab;
            cab.setEnabled(true);
            cab.setType(cabType);
            cab.prepare((double)sr, (int)samples.size());
            cab.process(samples.data(), (int)samples.size());
            for (auto& s : samples) s = std::clamp(s, -1.0f, 1.0f);

        } else {
            std::cerr << "Unknown effect: " << effect << "\n";
            return 1;
        }

        writeWav(outputPath, samples, sr);
        std::cout << "Written: " << outputPath << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
