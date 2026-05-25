# RACK_SDK_DIR is set by the parent CMakeLists.txt.
# Build the plugin as a MODULE (dlopen-able dylib on macOS).

add_library(Distant-EchoRack MODULE
    src/plugin.cpp
    src/OverdriveModule.cpp
    src/DelayModule.cpp
)

# Rack expects the binary to be named exactly "plugin.dylib".
set_target_properties(Distant-EchoRack PROPERTIES
    PREFIX      ""
    SUFFIX      ".dylib"
    OUTPUT_NAME "plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

target_include_directories(Distant-EchoRack PRIVATE
    "${RACK_SDK_DIR}/include"
    "${RACK_SDK_DIR}/dep/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(Distant-EchoRack PRIVATE effects)

# Match flags from Rack's compile.mk / plugin.mk (macOS ARM64)
target_compile_options(Distant-EchoRack PRIVATE
    -fPIC
    -O3
    -funsafe-math-optimizations
    -fno-omit-frame-pointer
    -Wall -Wextra -Wno-unused-parameter
    -march=armv8-a+fp+simd
    -stdlib=libc++
)

# -undefined dynamic_lookup allows Rack's symbols to be resolved at runtime
# from the host process instead of requiring a direct link to libRack.
# We still pass -lRack so the plugin works when Rack is itself loaded as a plugin.
target_link_options(Distant-EchoRack PRIVATE
    "-L${RACK_SDK_DIR}"
    "-lRack"
    "-undefined" "dynamic_lookup"
    "-stdlib=libc++"
)

# ---------------------------------------------------------------------------
# Install target: copy plugin.dylib + resources into the Rack2 plugins dir.
# Usage: cmake --build build --target install-rack-plugin
# ---------------------------------------------------------------------------
set(RACK_PLUGINS_DIR "$ENV{HOME}/Library/Application Support/Rack2/plugins-mac-arm64")
set(PLUGIN_INSTALL_DIR "${RACK_PLUGINS_DIR}/Distant-Echo")

add_custom_target(install-rack-plugin
    DEPENDS Distant-EchoRack
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_INSTALL_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:Distant-EchoRack>"
        "${PLUGIN_INSTALL_DIR}/plugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "${PLUGIN_INSTALL_DIR}/plugin.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res"
        "${PLUGIN_INSTALL_DIR}/res"
    # Fix the embedded libRack path so Rack can find it at runtime
    COMMAND install_name_tool -change libRack.dylib /tmp/Rack2/libRack.dylib
        "${PLUGIN_INSTALL_DIR}/plugin.dylib"
    COMMAND codesign -f -s - "${PLUGIN_INSTALL_DIR}/plugin.dylib"
    COMMENT "Installing Distant-Echo plugin → ${PLUGIN_INSTALL_DIR}"
)

# ---------------------------------------------------------------------------
# Distribution target: produce Distant-Echo-2.0.0-mac-arm64.vcvplugin
# Usage: cmake --build build --target dist-rack-plugin
# Requires: zstd (brew install zstd)
# ---------------------------------------------------------------------------
set(DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
set(DIST_SLUG "Distant-Echo")
set(DIST_VERSION "2.0.0")

add_custom_target(dist-rack-plugin
    DEPENDS Distant-EchoRack
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory   "${DIST_DIR}/${DIST_SLUG}/res"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:Distant-EchoRack>"
        "${DIST_DIR}/${DIST_SLUG}/plugin.dylib"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "${DIST_DIR}/${DIST_SLUG}/plugin.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/res"
        "${DIST_DIR}/${DIST_SLUG}/res"
    COMMAND strip -S "${DIST_DIR}/${DIST_SLUG}/plugin.dylib"
    COMMAND install_name_tool -change libRack.dylib /tmp/Rack2/libRack.dylib
        "${DIST_DIR}/${DIST_SLUG}/plugin.dylib"
    COMMAND codesign -f -s - "${DIST_DIR}/${DIST_SLUG}/plugin.dylib"
    COMMAND tar --no-xattrs -C "${DIST_DIR}" -c "${DIST_SLUG}"
        | zstd -19 -o "${DIST_DIR}/${DIST_SLUG}-${DIST_VERSION}-mac-arm64.vcvplugin"
    COMMENT "Packaging ${DIST_SLUG}-${DIST_VERSION}-mac-arm64.vcvplugin"
)
