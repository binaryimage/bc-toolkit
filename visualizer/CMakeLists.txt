cmake_minimum_required(VERSION 3.22)

project(BCVisualizer VERSION 1.2.0)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TOOLKIT_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(TOOLKIT_JUCE_DIR AND EXISTS ${TOOLKIT_JUCE_DIR}/CMakeLists.txt)
    add_subdirectory(${TOOLKIT_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(TOOLKIT_HEADLESS_LINUX "Build without ALSA/Xrandr/Xinerama/Xcursor (CI screenshots)" OFF)

# Shares the Toolkit's look, widgets and fonts. Its Source/ (minus Main.cpp) is also compiled into BC Toolkit as the Video page.
set(TK ${CMAKE_CURRENT_SOURCE_DIR}/../toolkit/pc)

juce_add_binary_data(VisualizerAssets SOURCES
    ${TK}/Resources/IBMPlexSans-Regular.ttf
    ${TK}/Resources/IBMPlexSans-Medium.ttf
    ${TK}/Resources/IBMPlexSans-SemiBold.ttf
    ${TK}/Resources/IBMPlexMono-Medium.ttf)

juce_add_gui_app(BCVisualizer
    PRODUCT_NAME "BC Visualizer"
    COMPANY_NAME "binaryimage"
    ICON_BIG ${TK}/Resources/icon.png
    VERSION ${PROJECT_VERSION})

target_sources(BCVisualizer PRIVATE
    Source/Main.cpp
    Source/Analysis.cpp
    Source/Visualizers.cpp
    Source/Renderer.cpp
    Source/Session.cpp
    Source/Video.cpp
    ${TK}/Source/Common/Look.cpp
    ${TK}/Source/Common/Widgets.cpp
    ${TK}/Source/Common/Dsp.cpp)

target_include_directories(BCVisualizer PRIVATE Source ${TK}/Source)

set(VIZ_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
if(TOOLKIT_HEADLESS_LINUX)
    list(APPEND VIZ_DEFS JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

target_compile_definitions(BCVisualizer PRIVATE ${VIZ_DEFS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BCVisualizer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BCVisualizer,JUCE_VERSION>")

target_link_libraries(BCVisualizer PRIVATE
    VisualizerAssets
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(WIN32)
    target_link_libraries(BCVisualizer PRIVATE mfplat mfreadwrite mfuuid ole32)
endif()
