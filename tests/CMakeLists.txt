add_executable(effects_tests
    test_overdrive.cpp
    test_delay.cpp
    test_cabinet.cpp
    test_biquad.cpp
    test_envelope.cpp
)

target_link_libraries(effects_tests PRIVATE effects Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(effects_tests)

# ---------------------------------------------------------------------------
# WAV comparison tool — processes input WAVs through C++ effects and writes
# output WAVs for comparison against Python golden files via compare.py.
# Usage: see Makefile targets 'validate' and 'compare'.
# ---------------------------------------------------------------------------
add_executable(wav_compare wav_compare.cpp)
target_link_libraries(wav_compare PRIVATE effects)
