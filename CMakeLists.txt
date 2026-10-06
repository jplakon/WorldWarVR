cmake_minimum_required(VERSION 3.25)

project(
    WorldAtWarVR
    VERSION 0.4.0
    DESCRIPTION "Standalone OpenXR compatibility mod for Call of Duty: World at War Zombies"
    LANGUAGES C CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "WorldAtWarVR currently targets 32-bit Windows only")
endif()

if(CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure with the Win32/x86 platform; the game process is 32-bit")
endif()

option(WAWVR_BUILD_TESTS "Build unit and validation tests" ON)
option(WAWVR_BUILD_XR "Build the OpenXR integration when its SDK is available" ON)
option(WAWVR_BUILD_TOOLS "Build local reverse-engineering diagnostic tools" ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Normalize compiler and PE/COFF linker metadata so two clean release
    # trees can be compared byte-for-byte before launcher publication.
    add_compile_options(/Brepro)
    add_link_options(/Brepro)
endif()

add_library(wawvr_project_options INTERFACE)
target_compile_definitions(
    wawvr_project_options
    INTERFACE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
)

if(MSVC)
    target_compile_options(
        wawvr_project_options
        INTERFACE
            /W4
            /permissive-
            /EHsc
            /utf-8
            /Zc:__cplusplus
    )
endif()

include(CTest)

if(WAWVR_BUILD_XR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr-sdk/CMakeLists.txt")
    # Only the static loader and public headers are needed by the D3D11 mod.
    # Disable host-environment Vulkan package discovery so SDK headers and
    # libraries cannot enter this D3D11-only loader build.
    set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON CACHE BOOL "" FORCE)
    set(BUILD_LOADER ON CACHE BOOL "" FORCE)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SPECIFICATION OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        third_party/openxr-sdk
        third_party/openxr-sdk-build
        EXCLUDE_FROM_ALL
    )

    if(TARGET openxr_loader)
        set_property(
            TARGET openxr_loader
            PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/t4/CMakeLists.txt")
    add_subdirectory(src/t4)
endif()

if(WAWVR_BUILD_XR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/xr/CMakeLists.txt")
    add_subdirectory(src/xr)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/CMakeLists.txt")
    add_subdirectory(src/gameplay)
endif()

add_subdirectory(src/mod)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/launcher/CMakeLists.txt")
    add_subdirectory(launcher)
endif()

if(WAWVR_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/process_probe/CMakeLists.txt")
    add_subdirectory(tools/process_probe)
endif()
