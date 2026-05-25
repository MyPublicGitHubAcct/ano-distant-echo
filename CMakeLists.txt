cmake_minimum_required(VERSION 3.22)
project(Distant-Echo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Shared effects library
# ---------------------------------------------------------------------------
add_subdirectory(libs/effects)

# ---------------------------------------------------------------------------
# Catch2 tests
# ---------------------------------------------------------------------------
find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
    enable_testing()
    add_subdirectory(tests)
else()
    message(STATUS "Catch2 not found — skipping tests (brew install catch2)")
endif()

# ---------------------------------------------------------------------------
# JUCE plugin (optional — set BUILD_JUCE_PLUGIN=ON to build)
# ---------------------------------------------------------------------------
option(BUILD_JUCE_PLUGIN "Build the JUCE audio plugin" OFF)
if(BUILD_JUCE_PLUGIN)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/juce" CACHE PATH "Path to JUCE source root")
    if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Run: git submodule update --init libs/juce")
    endif()
    add_subdirectory("${JUCE_DIR}" juce EXCLUDE_FROM_ALL)
    add_subdirectory(juce-plugin)
else()
    message(STATUS "BUILD_JUCE_PLUGIN=OFF — skipping juce-plugin (cmake -DBUILD_JUCE_PLUGIN=ON to enable)")
endif()

# ---------------------------------------------------------------------------
# VCV Rack module (optional — requires Rack SDK to be present)
# ---------------------------------------------------------------------------
set(RACK_SDK_DIR "$ENV{HOME}/Rack-SDK" CACHE PATH "Path to VCV Rack SDK root (leave empty to skip)")
if(RACK_SDK_DIR)
    add_subdirectory(vcv-rack)
else()
    message(STATUS "RACK_SDK_DIR not set — skipping vcv-rack target")
endif()
