cmake_minimum_required(VERSION 3.22)

# Meter Bridge is a Mac app: universal binary, macOS 12 or later.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(MeterBridge VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# JUCE is pinned to the same release as Prism Meter. Point MB_JUCE_DIR at a
# local checkout to build offline.
set(MB_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
if(MB_JUCE_DIR)
    add_subdirectory(${MB_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# Prism Meter's DSP core (no JUCE).
add_subdirectory(third_party/prism/core)

set(PRISM_SOURCE ${CMAKE_SOURCE_DIR}/third_party/prism/plugin/Source)

juce_add_gui_app(MeterBridge
    PRODUCT_NAME "Meter Bridge"
    COMPANY_NAME "New Constellation"
    BUNDLE_ID "com.newconstellation.meterbridge"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_SOURCE_DIR}/Resources/AppIcon-1024.png
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Meter Bridge reads audio inputs to show levels and spectra. Nothing is recorded or sent anywhere."
)

target_sources(MeterBridge PRIVATE
    app/Source/Main.cpp
    app/Source/AppSettings.cpp
    app/Source/AudioEngine.cpp
    app/Source/MeterHub.cpp
    app/Source/DisplayWindow.cpp
    app/Source/GridView.cpp
    app/Source/OverlayView.cpp
    app/Source/SettingsWindow.cpp
    ${PRISM_SOURCE}/PluginProcessor.cpp
    ${PRISM_SOURCE}/PluginEditor.cpp
    ${PRISM_SOURCE}/ui/MeterViews.cpp
    ${PRISM_SOURCE}/ui/MeterModules.cpp
    ${PRISM_SOURCE}/ui/RadarView.cpp
)

target_include_directories(MeterBridge PRIVATE app/Source ${PRISM_SOURCE})

target_compile_definitions(MeterBridge PRIVATE
    JucePlugin_Name="Meter Bridge"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MeterBridge,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MeterBridge,JUCE_VERSION>"
)

target_link_libraries(MeterBridge
    PRIVATE
        prism_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Developer tool: renders a window to PNG with synthetic keyboards (no audio device).
option(MB_BUILD_SNAPSHOT_TOOL "Build the MeterBridgeSnapshot screenshot tool" OFF)
if(MB_BUILD_SNAPSHOT_TOOL)
    juce_add_console_app(MeterBridgeSnapshot PRODUCT_NAME "MeterBridgeSnapshot")
    get_target_property(MB_APP_SOURCES MeterBridge SOURCES)
    list(FILTER MB_APP_SOURCES EXCLUDE REGEX "Main\\.cpp$")
    target_sources(MeterBridgeSnapshot PRIVATE tools/Snapshot.cpp ${MB_APP_SOURCES})
    target_include_directories(MeterBridgeSnapshot PRIVATE app/Source ${PRISM_SOURCE})
    target_compile_definitions(MeterBridgeSnapshot PRIVATE
        JucePlugin_Name="Meter Bridge"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MeterBridgeSnapshot PRIVATE
        prism_core juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
