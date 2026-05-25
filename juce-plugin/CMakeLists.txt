juce_add_plugin(Distant-Echo
    PRODUCT_NAME        "Distant-Echo"
    COMPANY_NAME        "Distant-Echo"
    PLUGIN_MANUFACTURER_CODE Anoe
    PLUGIN_CODE         Anoe
    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    NEEDS_MIDI_OUTPUT   FALSE
    IS_MIDI_EFFECT      FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS             VST3 AU Standalone
)

target_sources(Distant-Echo PRIVATE
    AnoLookAndFeel.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
)

target_compile_definitions(Distant-Echo PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Distant-Echo PRIVATE
    effects
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Install targets: copy JUCE plugin formats to macOS user plugin directories.
# Must build with -DBUILD_JUCE_PLUGIN=ON first.
#
# Usage:
#   cmake --build build --target install-vst3        # ~/Library/Audio/Plug-Ins/VST3/
#   cmake --build build --target install-au          # ~/Library/Audio/Plug-Ins/Components/
#   cmake --build build --target install-standalone  # ~/Applications/
#   cmake --build build --target install-juce        # all three
# ---------------------------------------------------------------------------
set(JUCE_ARTEFACTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/Distant-Echo_artefacts")

# JUCE signs the bundle before the Steinberg SDK injects moduleinfo.json, which
# invalidates the signature. Re-sign with ad-hoc identity after all post-build
# steps so the bundle verifies cleanly.
add_custom_command(TARGET Distant-Echo_VST3 POST_BUILD
    COMMAND codesign --force --deep --sign -
        "${JUCE_ARTEFACTS_DIR}/VST3/Distant-Echo.vst3"
    COMMENT "Re-signing Distant-Echo.vst3 after post-build resource injection"
    VERBATIM
)
set(USER_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
set(USER_AU_DIR   "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
set(USER_APPS_DIR "$ENV{HOME}/Applications")

add_custom_target(install-vst3
    DEPENDS Distant-Echo_VST3
    COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_VST3_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${JUCE_ARTEFACTS_DIR}/VST3/Distant-Echo.vst3"
        "${USER_VST3_DIR}/Distant-Echo.vst3"
    COMMENT "Installing Distant-Echo.vst3 → ${USER_VST3_DIR}"
)

add_custom_target(install-au
    DEPENDS Distant-Echo_AU
    COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_AU_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${JUCE_ARTEFACTS_DIR}/AU/Distant-Echo.component"
        "${USER_AU_DIR}/Distant-Echo.component"
    COMMAND bash -c "killall -9 AudioComponentRegistrar 2>/dev/null || true"
    COMMENT "Installing Distant-Echo.component → ${USER_AU_DIR}"
    VERBATIM
)

add_custom_target(install-standalone
    DEPENDS Distant-Echo_Standalone
    COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_APPS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${JUCE_ARTEFACTS_DIR}/Standalone/Distant-Echo.app"
        "${USER_APPS_DIR}/Distant-Echo.app"
    COMMENT "Installing Distant-Echo.app → ${USER_APPS_DIR}"
)

add_custom_target(install-juce
    DEPENDS install-vst3 install-au install-standalone
    COMMENT "Installed Distant-Echo VST3, AU component, and Standalone app"
)

# ---------------------------------------------------------------------------
# LookAndFeel unit tests — built only when Catch2 is available.
# Run via: ctest --test-dir build -R laf   (after cmake -DBUILD_JUCE_PLUGIN=ON)
# ---------------------------------------------------------------------------
find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
    enable_testing()
    juce_add_console_app(laf_tests PRODUCT_NAME "LAFTests")
    target_sources(laf_tests PRIVATE
        test_laf.cpp
        AnoLookAndFeel.cpp
    )
    target_compile_definitions(laf_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(laf_tests PRIVATE
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        Catch2::Catch2
    )
    include(Catch)
    catch_discover_tests(laf_tests)
endif()
