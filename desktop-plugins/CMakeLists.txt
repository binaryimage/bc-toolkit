cmake_minimum_required(VERSION 3.22)

# binaryimage's MPC plugins as desktop VST3s. Each plugin's MPC source (../mpc/<name>/src) is compiled in
# unchanged, with the bridge in bridge/ and the plugin's MPC skin built in for the window.
project(BCPlugins VERSION 1.0.0)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BCP_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(BCP_JUCE_DIR AND EXISTS ${BCP_JUCE_DIR}/CMakeLists.txt)
    add_subdirectory(${BCP_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(BCP_HEADLESS_LINUX "Linux build without ALSA/Xrandr/Xinerama/Xcursor (local checks)" OFF)
option(BCP_CHECKS "Also build the <Plugin>Check test programs" ON)

set(MPC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mpc)
set(BRIDGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bridge)

set(BCP_DEFS
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0 JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 _USE_MATH_DEFINES NOMINMAX)
if(BCP_HEADLESS_LINUX)
    list(APPEND BCP_DEFS JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

# bcp_plugin(<Name> <mpc folder> <4-char code> <SYNTH|FX> [skins folder])
function(bcp_plugin NAME DIR CODE KIND)
    set(SKINS ${MPC_DIR}/${DIR}/skins)
    if(ARGC GREATER 4)
        set(SKINS ${ARGV4})
    endif()
    file(GLOB SKIN_FILES ${SKINS}/TUI.json ${SKINS}/*.png)
    file(GLOB PLUGIN_SOURCES ${MPC_DIR}/${DIR}/src/*.cpp)

    juce_add_binary_data(${NAME}Skin NAMESPACE SkinData HEADER_NAME SkinData.h SOURCES ${SKIN_FILES})
    set_target_properties(${NAME}Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(KIND STREQUAL "SYNTH")
        set(SYNTH TRUE)
        set(CATS "Instrument Synth")
    else()
        set(SYNTH FALSE)
        set(CATS "Fx")
    endif()

    juce_add_plugin(${NAME}
        PRODUCT_NAME "${NAME}"
        COMPANY_NAME "binaryimage"
        COMPANY_WEBSITE "https://github.com/binaryimage/bc-toolkit"
        BUNDLE_ID "com.binaryimage.${NAME}"
        PLUGIN_MANUFACTURER_CODE Bnry
        PLUGIN_CODE ${CODE}
        IS_SYNTH ${SYNTH}
        NEEDS_MIDI_INPUT ${SYNTH}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES ${CATS}
        COPY_PLUGIN_AFTER_BUILD FALSE
        FORMATS VST3)

    set(COMMON_SOURCES ${PLUGIN_SOURCES} ${BRIDGE_DIR}/MpcBridge.cpp ${BRIDGE_DIR}/MpcEditor.cpp)
    target_sources(${NAME} PRIVATE ${COMMON_SOURCES})
    target_include_directories(${NAME} PRIVATE ${BRIDGE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${MPC_DIR}/${DIR}/src)
    target_compile_definitions(${NAME} PRIVATE ${BCP_DEFS} BCP_IS_SYNTH=$<BOOL:${SYNTH}>)
    target_link_libraries(${NAME} PRIVATE ${NAME}Skin juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    if(BCP_CHECKS)
        juce_add_console_app(${NAME}Check PRODUCT_NAME "${NAME}Check")
        target_sources(${NAME}Check PRIVATE ${COMMON_SOURCES} ${BRIDGE_DIR}/BridgeCheck.cpp)
        target_include_directories(${NAME}Check PRIVATE ${BRIDGE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/sdk ${MPC_DIR}/${DIR}/src)
        target_compile_definitions(${NAME}Check PRIVATE ${BCP_DEFS} BCP_IS_SYNTH=$<BOOL:${SYNTH}> JucePlugin_Name="${NAME}"
            JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(${NAME}Check PRIVATE ${NAME}Skin juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics
            juce::juce_audio_formats juce::juce_recommended_config_flags)
    endif()
endfunction()

bcp_plugin(Janus     janus     Jans SYNTH ${CMAKE_CURRENT_SOURCE_DIR}/skins/janus)
bcp_plugin(Maggot    maggot    Mggt SYNTH)
bcp_plugin(Legion    legion    Lgon SYNTH)
bcp_plugin(Grendel   grendel   Grnd FX)
bcp_plugin(Ouroboros ouroboros Ourb FX)
bcp_plugin(Fenrir    fenrir    Fnrr FX)
bcp_plugin(Moloch    moloch    Mlch FX)
bcp_plugin(Mare      mare      Mare FX)
bcp_plugin(Banshee   banshee   Bnsh FX)

if(BCP_CHECKS AND WIN32)
    # loads the built .vst3 files through the VST3 interfaces, like a DAW
    juce_add_console_app(VST3Probe PRODUCT_NAME "VST3Probe")
    target_sources(VST3Probe PRIVATE tools/VST3Probe.cpp)
    target_compile_definitions(VST3Probe PRIVATE ${BCP_DEFS} JUCE_PLUGINHOST_VST3=1 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(VST3Probe PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
endif()
