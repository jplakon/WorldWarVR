cmake_minimum_required(VERSION 3.24)

project(WaWVRLauncher VERSION 0.4.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "WaWVR Launcher currently supports Windows only.")
endif()

set(WAWVR_PEZBOT_IMPORT_HELPER
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/import_pezbot.ps1")
file(SHA256
    "${WAWVR_PEZBOT_IMPORT_HELPER}"
    WAWVR_PEZBOT_IMPORT_HELPER_SHA256)
string(TOUPPER
    "${WAWVR_PEZBOT_IMPORT_HELPER_SHA256}"
    WAWVR_PEZBOT_IMPORT_HELPER_SHA256)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pezbot_helper_hash.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/pezbot_helper_hash.hpp"
    @ONLY)

add_library(wawvr_launcher_core STATIC
    src/launcher_core.cpp
)

target_include_directories(wawvr_launcher_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_features(wawvr_launcher_core PUBLIC cxx_std_20)
target_compile_definitions(wawvr_launcher_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wawvr_launcher_core PUBLIC bcrypt version shell32)
if(TARGET wawvr_t4)
    target_link_libraries(wawvr_launcher_core PRIVATE wawvr_t4)
    target_compile_definitions(
        wawvr_launcher_core PRIVATE WAWVR_HAS_T4_PROFILE=1)
endif()

add_executable(wawvr-launcher
    src/main.cpp
)
target_link_libraries(wawvr-launcher PRIVATE wawvr_launcher_core)
set_target_properties(wawvr-launcher PROPERTIES OUTPUT_NAME "wawvr-launcher-cli")

# Native x86 helper used by the adjacent WinUI shell. It is a GUI-subsystem
# executable so launching from the settings window and the later stock SP -> MP
# handoff never flashes a console. The shell invokes this exact artifact with
# explicit CLI options; if run directly with no arguments it retains the safe
# packaged main-menu default.
add_executable(WorldAtWarVRNativeHelper WIN32
    src/main.cpp
)
target_compile_definitions(
    WorldAtWarVRNativeHelper PRIVATE WAWVR_DEFAULT_PLAY=1 WAWVR_GUI=1)
target_link_libraries(
    WorldAtWarVRNativeHelper PRIVATE wawvr_launcher_core user32)
set_target_properties(
    WorldAtWarVRNativeHelper PROPERTIES OUTPUT_NAME "wawvr-launcher")

# User-facing entry point. With no arguments this launches the stock frontend
# for Zombies map selection and injects the adjacent WorldWarVR.dll;
# command-line arguments retain the diagnostic interface while still defaulting
# to the adjacent DLL unless --mod-dll explicitly overrides it. Preparation may
# copy this exact built artifact into the isolated SP runtime as CoDWaWmp.exe;
# that runtime-only basename activates the verified SP-parent handoff. No target
# or package artifact is built under the proprietary game's executable name.
add_executable(WorldAtWarVRLauncher WIN32
    src/main.cpp
)
target_compile_definitions(
    WorldAtWarVRLauncher PRIVATE WAWVR_DEFAULT_PLAY=1 WAWVR_GUI=1)
target_link_libraries(WorldAtWarVRLauncher PRIVATE wawvr_launcher_core user32)
set_target_properties(WorldAtWarVRLauncher PROPERTIES OUTPUT_NAME "WorldWarVR")

# Dedicated Explorer-friendly entry point for offline multiplayer and PeZBOT.
# The distinct build target binds the default mode to the binary instead of
# relying on a filename-sensitive copy or transient command-line arguments.
add_executable(WorldAtWarVRMultiplayerLauncher WIN32
    src/main.cpp
)
target_compile_definitions(
    WorldAtWarVRMultiplayerLauncher PRIVATE
    WAWVR_DEFAULT_PLAY=1 WAWVR_DEFAULT_MULTIPLAYER=1 WAWVR_GUI=1)
target_link_libraries(
    WorldAtWarVRMultiplayerLauncher PRIVATE wawvr_launcher_core user32)
set_target_properties(
    WorldAtWarVRMultiplayerLauncher PROPERTIES
    OUTPUT_NAME "WorldWarVR-Multiplayer")

foreach(target
        wawvr-launcher
        WorldAtWarVRNativeHelper
        WorldAtWarVRLauncher
        WorldAtWarVRMultiplayerLauncher)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WAWVR_PEZBOT_IMPORT_HELPER}"
            "$<TARGET_FILE_DIR:${target}>/WaWVR-PeZBOT-Import.ps1"
        VERBATIM)
endforeach()

# Keep both branded build-tree executables directly runnable. The standalone
# packager already places the DLL beside them, but developers and diagnostics
# must see the same adjacent-file layout before packaging as well.
foreach(target WorldAtWarVRLauncher WorldAtWarVRMultiplayerLauncher)
    add_dependencies(${target} WorldAtWarVR)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:WorldAtWarVR>"
            "$<TARGET_FILE_DIR:${target}>/WorldWarVR.dll"
        VERBATIM)
endforeach()

if(MSVC)
    foreach(target
            wawvr_launcher_core
            wawvr-launcher
            WorldAtWarVRNativeHelper
            WorldAtWarVRLauncher
            WorldAtWarVRMultiplayerLauncher)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(wawvr-launcher-tests
        tests/launcher_tests.cpp
    )
    target_link_libraries(wawvr-launcher-tests PRIVATE wawvr_launcher_core)
    add_custom_command(TARGET wawvr-launcher-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WAWVR_PEZBOT_IMPORT_HELPER}"
            "$<TARGET_FILE_DIR:wawvr-launcher-tests>/WaWVR-PeZBOT-Import.ps1"
        VERBATIM)
    if(MSVC)
        target_compile_options(wawvr-launcher-tests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME wawvr-launcher-tests COMMAND wawvr-launcher-tests)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING
        "This is a 64-bit diagnostic build. Build with `-A Win32` to inject a DLL "
        "into the 32-bit World at War process.")
endif()
