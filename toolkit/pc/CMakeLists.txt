cmake_minimum_required(VERSION 3.22)

project(BinaryimageToolkit VERSION 1.5.0)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if one is given, otherwise download it.
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

# Lean Linux build used only for headless tests and screenshots (no audio devices, no X extensions).
option(TOOLKIT_HEADLESS_LINUX "Build without ALSA/Xrandr/Xinerama/Xcursor (CI screenshots)" OFF)
option(TOOLKIT_BUILD_TESTS "Build the test runner" ON)

juce_add_binary_data(ToolkitAssets SOURCES
    Resources/IBMPlexSans-Regular.ttf
    Resources/IBMPlexSans-Medium.ttf
    Resources/IBMPlexSans-SemiBold.ttf
    Resources/IBMPlexMono-Medium.ttf
    Resources/books/patchbook-2600.json
    Resources/books/messenger-patches.json
    Resources/books/hydrasynth-patches.json
    Resources/books/leviasynth-patches.json
    Resources/books/minifreak-patches.json
    Resources/books/messenger-controls.json
    Resources/tutorial.md)

set(TOOLKIT_COMMON_SOURCES
    Source/Common/Dsp.cpp
    Source/Common/Look.cpp
    Source/Common/Widgets.cpp
    Source/Common/Files.cpp
    Source/Common/Link.cpp
    Source/Common/Wavetable.cpp
    Source/Common/Mangle.cpp
    Source/Common/Theory.cpp
    Source/Common/LayoutCheck.cpp)

set(TOOLKIT_APP_SOURCES
    Source/App/Engine.cpp
    Source/App/MainWindow.cpp
    Source/App/PageMeters.cpp
    Source/App/PageRecord.cpp
    Source/App/WinLoopback.cpp
    Source/App/PageBackup.cpp
    Source/App/PageSampler.cpp
    Source/App/PageGearCheck.cpp
    Source/App/PageChop.cpp
    Source/App/PageLibrary.cpp
    Source/App/PageWaves.cpp
    Source/App/PageMangle.cpp
    Source/App/PageRiffs.cpp
    Source/App/PageBooks.cpp
    Source/App/PageVideo.cpp
    Source/App/PageHelp.cpp
    Source/App/PageSettings.cpp
    Source/App/Settings.cpp)

# The Video page is the BC Visualizer (visualizer/Source): its styles, renderer and MP4 encoder are shared, not copied.
set(VISUALIZER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../visualizer/Source)
list(APPEND TOOLKIT_APP_SOURCES
    ${VISUALIZER_DIR}/Analysis.cpp
    ${VISUALIZER_DIR}/Visualizers.cpp
    ${VISUALIZER_DIR}/Trippy.cpp
    ${VISUALIZER_DIR}/Renderer.cpp
    ${VISUALIZER_DIR}/Session.cpp
    ${VISUALIZER_DIR}/Video.cpp
    ${VISUALIZER_DIR}/Patterns.cpp
    ${VISUALIZER_DIR}/Parallel.cpp)
if(WIN32)
    set(TOOLKIT_VIDEO_LIBS mfplat mfreadwrite mfuuid ole32)   # Media Foundation writes the MP4
else()
    set(TOOLKIT_VIDEO_LIBS "")
endif()

set(TOOLKIT_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_ASIO=1)   # your interface's ASIO driver (JUCE 8 ships the SDK headers)
if(TOOLKIT_HEADLESS_LINUX)
    list(APPEND TOOLKIT_DEFS JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

set(TOOLKIT_MODULES
    juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_dsp
    juce::juce_gui_extra juce::juce_gui_basics juce::juce_graphics juce::juce_events juce::juce_core)

# ---------------------------------------------------------------- the standalone app
juce_add_gui_app(BinaryimageToolkit
    PRODUCT_NAME "BC Toolkit"
    COMPANY_NAME "BC"
    VERSION ${PROJECT_VERSION}
    ICON_BIG Resources/icon.png
    ICON_SMALL Resources/icon.png)
target_sources(BinaryimageToolkit PRIVATE Source/App/Main.cpp split/SplitCore.cpp split/SplitPlatform.cpp ${TOOLKIT_APP_SOURCES} ${TOOLKIT_COMMON_SOURCES})
target_include_directories(BinaryimageToolkit PRIVATE Source split ${VISUALIZER_DIR})
target_compile_definitions(BinaryimageToolkit PRIVATE ${TOOLKIT_DEFS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BinaryimageToolkit,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BinaryimageToolkit,JUCE_VERSION>")
target_link_libraries(BinaryimageToolkit PRIVATE ToolkitAssets ${TOOLKIT_MODULES} ${TOOLKIT_VIDEO_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- BC Split host (owns the real interface)
juce_add_gui_app(SplitHost
    PRODUCT_NAME "BC Split Host"
    COMPANY_NAME "BC"
    VERSION ${PROJECT_VERSION}
    ICON_BIG Resources/icon.png
    ICON_SMALL Resources/icon.png)
target_sources(SplitHost PRIVATE Source/Split/HostMain.cpp split/SplitCore.cpp split/SplitPlatform.cpp ${TOOLKIT_COMMON_SOURCES})
target_include_directories(SplitHost PRIVATE Source split)
target_compile_definitions(SplitHost PRIVATE ${TOOLKIT_DEFS}
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SplitHost,JUCE_VERSION>")
target_link_libraries(SplitHost PRIVATE ToolkitAssets ${TOOLKIT_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- the VST3 that lives in Ableton
if(NOT TOOLKIT_HEADLESS_LINUX)
    set(TOOLKIT_LINK_FORMATS VST3)
else()
    set(TOOLKIT_LINK_FORMATS VST3)
endif()
juce_add_plugin(ToolkitLink
    COMPANY_NAME                "BC"
    BUNDLE_ID                   com.binaryimage.toolkitlink
    PLUGIN_MANUFACTURER_CODE    Bnim
    PLUGIN_CODE                 Tlnk
    FORMATS                     ${TOOLKIT_LINK_FORMATS}
    PRODUCT_NAME                "BC Link"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Analyzer)
target_sources(ToolkitLink PRIVATE Source/Plugin/LinkProcessor.cpp Source/Plugin/LinkEditor.cpp ${TOOLKIT_COMMON_SOURCES})
target_include_directories(ToolkitLink PRIVATE Source)
target_compile_definitions(ToolkitLink PUBLIC ${TOOLKIT_DEFS} JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ToolkitLink PRIVATE ToolkitAssets ${TOOLKIT_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests + screenshots
if(TOOLKIT_BUILD_TESTS)
    juce_add_console_app(ToolkitTests PRODUCT_NAME "ToolkitTests")
    target_sources(ToolkitTests PRIVATE tests/Tests.cpp split/SplitCore.cpp split/SplitPlatform.cpp Source/Plugin/LinkProcessor.cpp Source/Plugin/LinkEditor.cpp ${TOOLKIT_APP_SOURCES} ${TOOLKIT_COMMON_SOURCES})
    target_include_directories(ToolkitTests PRIVATE Source split ${VISUALIZER_DIR})
    target_compile_definitions(ToolkitTests PRIVATE ${TOOLKIT_DEFS} TOOLKIT_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ToolkitTests PRIVATE ToolkitAssets ${TOOLKIT_MODULES} ${TOOLKIT_VIDEO_LIBS}
        PUBLIC juce::juce_recommended_config_flags)
endif()
