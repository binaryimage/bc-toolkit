# Desktop (Linux) build of the phone app, for CI: screenshots of every tab at phone sizes, the button check,
# and export tests with Patterns (--selftest <folder>). The phone build itself comes from BCVisualizer.jucer.
cmake_minimum_required(VERSION 3.22)

project(BCVisualizerPhone VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TOOLKIT_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(TOOLKIT_JUCE_DIR AND EXISTS ${TOOLKIT_JUCE_DIR}/CMakeLists.txt)
    add_subdirectory(${TOOLKIT_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(VZ ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
set(TK ${CMAKE_CURRENT_SOURCE_DIR}/../../toolkit/pc)

juce_add_binary_data(PhoneAssets SOURCES
    ${TK}/Resources/IBMPlexSans-Regular.ttf
    ${TK}/Resources/IBMPlexSans-Medium.ttf
    ${TK}/Resources/IBMPlexSans-SemiBold.ttf
    ${TK}/Resources/IBMPlexMono-Medium.ttf)

juce_add_gui_app(BCVisualizerPhone PRODUCT_NAME "BC Visualizer Phone" COMPANY_NAME "binaryimage" VERSION ${PROJECT_VERSION})

target_sources(BCVisualizerPhone PRIVATE
    Source/PhoneMain.cpp
    ${VZ}/Analysis.cpp ${VZ}/Visualizers.cpp ${VZ}/Trippy.cpp ${VZ}/Renderer.cpp ${VZ}/Session.cpp ${VZ}/Video.cpp
    ${VZ}/Patterns.cpp ${VZ}/Parallel.cpp
    ${TK}/Source/Common/Look.cpp ${TK}/Source/Common/Widgets.cpp ${TK}/Source/Common/LayoutCheck.cpp ${TK}/Source/Common/Dsp.cpp)

target_include_directories(BCVisualizerPhone PRIVATE ${VZ} ${TK}/Source)

target_compile_definitions(BCVisualizerPhone PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BCVisualizerPhone,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BCVisualizerPhone,JUCE_VERSION>")

target_link_libraries(BCVisualizerPhone PRIVATE
    PhoneAssets
    juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
