cmake_minimum_required(VERSION 3.22)

# Every plugin's name, code and version lives in plugins.json (also used by the release
# manifest the LiveTest Updater reads), so there is one place to bump a version.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/plugins.json" LTP_JSON)
string(JSON LTP_SUITE_VERSION GET "${LTP_JSON}" suiteVersion)
string(JSON LTP_UPDATER_VERSION GET "${LTP_JSON}" updaterVersion)

project(LiveTestPlugin VERSION ${LTP_SUITE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal binary
endif()
# Windows: static C++ runtime, so nothing extra needs installing
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(LTP_BUILD_PLUGINS "Build the VST3 plugins" ON)
option(LTP_BUILD_UPDATER "Build the LiveTest Updater app" ON)
option(LTP_BUILD_TESTS "Build the offline DSP test runner" OFF)
set(LTP_ONLY "" CACHE STRING "Optional ;-list of plugin ids to configure (empty = all)")
set(LTP_EXTRA_DEFINES "" CACHE STRING "Extra compile definitions (local Linux checks)")

set(LTP_SHARED_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared/Base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared/Ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Shared/Faceplate.cpp)

set(LTP_COMMON_DEFINES
    LTP_SUITE_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ${LTP_EXTRA_DEFINES})

string(JSON LTP_COUNT LENGTH "${LTP_JSON}" plugins)
math(EXPR LTP_LAST "${LTP_COUNT} - 1")
set(LTP_ALL_IDS "")

foreach(i RANGE ${LTP_LAST})
    string(JSON id      GET "${LTP_JSON}" plugins ${i} id)
    string(JSON name    GET "${LTP_JSON}" plugins ${i} name)
    string(JSON code    GET "${LTP_JSON}" plugins ${i} code)
    string(JSON version GET "${LTP_JSON}" plugins ${i} version)
    string(JSON family  GET "${LTP_JSON}" plugins ${i} family)
    string(JSON sq      GET "${LTP_JSON}" plugins ${i} sq)
    string(JSON modeled GET "${LTP_JSON}" plugins ${i} modeledOn)
    list(APPEND LTP_ALL_IDS ${id})
    # per-plugin identity, also attached to the source file for the combined test build
    set(LTP_DEFS_${id} LTP_NAME="${name}" LTP_SQ="${sq}" LTP_MODELED="${modeled}" LTP_PLUGIN_VERSION="${version}")

    if(NOT LTP_BUILD_PLUGINS)
        continue()
    endif()
    if(LTP_ONLY AND NOT id IN_LIST LTP_ONLY)
        continue()
    endif()

    if(family STREQUAL "Dynamics")
        set(cats "Fx" "Dynamics")
    else()
        set(cats "Fx")
    endif()

    string(TOLOWER "${id}" idLower)
    juce_add_plugin(LT_${id}
        COMPANY_NAME "LiveTest Audio"
        PRODUCT_NAME "${name}"
        VERSION ${version}
        BUNDLE_ID "com.livetest.${idLower}"
        PLUGIN_MANUFACTURER_CODE Ltst
        PLUGIN_CODE ${code}
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES ${cats}
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(LT_${id} PRIVATE Plugins/${id}/${id}.cpp ${LTP_SHARED_SOURCES})
    target_include_directories(LT_${id} PRIVATE Shared Plugins)
    target_compile_definitions(LT_${id} PUBLIC LTP_VERSION="${version}" ${LTP_DEFS_${id}} ${LTP_COMMON_DEFINES})
    target_link_libraries(LT_${id}
        PRIVATE
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endforeach()

if(LTP_BUILD_UPDATER)
    add_subdirectory(Updater)
endif()

if(LTP_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
