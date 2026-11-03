# BC Split: the ASIO driver DLL (built 64- and 32-bit) and its tests. Plain Win32, no JUCE.
cmake_minimum_required(VERSION 3.22)
project(BinaryimageSplit VERSION 1.0.0 LANGUAGES CXX)
# Split's own installer version is the version above
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")   # no VC++ redistributable needed
endif()

set(SPLIT_ASIO_DIR "" CACHE PATH "Folder holding asio.h, asiosys.h and iasiodrv.h (JUCE ships them)")

add_library(SplitCore STATIC SplitCore.cpp SplitPlatform.cpp)
target_include_directories(SplitCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(SplitCore PUBLIC Threads::Threads rt)
endif()

add_executable(SplitProtocolTest SplitProtocolTest.cpp)
target_link_libraries(SplitProtocolTest PRIVATE SplitCore)

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SPLIT_BITS x64)
    else()
        set(SPLIT_BITS x86)
    endif()
    add_library(SplitDriver SHARED SplitDriver.cpp SplitDriver.def)
    set_target_properties(SplitDriver PROPERTIES OUTPUT_NAME "BC Split ${SPLIT_BITS}")
    target_include_directories(SplitDriver PRIVATE ${SPLIT_ASIO_DIR})
    target_link_libraries(SplitDriver PRIVATE SplitCore ole32 advapi32 avrt winmm)

    add_executable(SplitDriverTest SplitDriverTest.cpp)
    target_include_directories(SplitDriverTest PRIVATE ${SPLIT_ASIO_DIR})
    target_link_libraries(SplitDriverTest PRIVATE ole32 advapi32)
endif()
