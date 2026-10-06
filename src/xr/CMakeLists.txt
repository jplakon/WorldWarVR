add_library(
    wawvr_xr
    STATIC
        compositor_math.cpp
        compositor_math.hpp
        d3d11_compositor.cpp
        d3d11_compositor.h
        d3d9_cpu_capture.cpp
        d3d9_cpu_capture.h
        d3d9ex_bridge.cpp
        d3d9ex_bridge.h
        d3d9ex_bridge_lifecycle.hpp
        openxr_runtime.cpp
        openxr_runtime.h
        runtime_policy.hpp
        frame_begin_lifecycle.hpp
        frame_timing_diagnostics.hpp
        xr_math.cpp
        xr_math.h
        xr_types.h
)

target_include_directories(wawvr_xr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    wawvr_xr
    PUBLIC
        wawvr_project_options
    PRIVATE
        d3d9
        d3d11
        dxgi
        d3dcompiler
)

if(TARGET OpenXR::openxr_loader)
    target_link_libraries(wawvr_xr PRIVATE OpenXR::openxr_loader)
elseif(TARGET openxr_loader)
    target_link_libraries(wawvr_xr PRIVATE openxr_loader)
else()
    message(FATAL_ERROR "wawvr_xr requires the official Khronos OpenXR loader")
endif()

if(WAWVR_BUILD_TESTS AND BUILD_TESTING)
    add_executable(wawvr_xr_frame_begin_tests frame_begin_lifecycle_test.cpp)
    target_link_libraries(wawvr_xr_frame_begin_tests PRIVATE wawvr_project_options)
    target_include_directories(wawvr_xr_frame_begin_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME xr.frame_begin_lifecycle COMMAND wawvr_xr_frame_begin_tests)

    add_executable(wawvr_xr_frame_timing_tests frame_timing_diagnostics_test.cpp)
    target_link_libraries(wawvr_xr_frame_timing_tests PRIVATE wawvr_project_options)
    target_include_directories(wawvr_xr_frame_timing_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME xr.frame_timing_diagnostics COMMAND wawvr_xr_frame_timing_tests)

    add_executable(wawvr_xr_math_tests xr_math_tests.cpp xr_math.cpp)
    target_link_libraries(wawvr_xr_math_tests PRIVATE wawvr_project_options)
    target_include_directories(
        wawvr_xr_math_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME xr.math_and_state COMMAND wawvr_xr_math_tests)

    add_executable(
        wawvr_xr_compositor_math_tests
        compositor_math.cpp
        compositor_math.hpp
        compositor_math_test.cpp
    )
    target_link_libraries(
        wawvr_xr_compositor_math_tests
        PRIVATE wawvr_project_options
    )
    target_include_directories(
        wawvr_xr_compositor_math_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME xr.compositor_math
        COMMAND wawvr_xr_compositor_math_tests
    )

    add_executable(
        wawvr_xr_d3d11_compositor_tests
        d3d11_compositor_test.cpp
    )
    target_link_libraries(
        wawvr_xr_d3d11_compositor_tests
        PRIVATE
            wawvr_project_options
            wawvr_xr
            d3dcompiler
    )
    target_include_directories(
        wawvr_xr_d3d11_compositor_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME xr.d3d11_compositor
        COMMAND wawvr_xr_d3d11_compositor_tests
    )

    add_executable(
        wawvr_xr_d3d9ex_bridge_tests
        d3d9ex_bridge_test.cpp
    )
    target_link_libraries(
        wawvr_xr_d3d9ex_bridge_tests
        PRIVATE
            wawvr_project_options
            wawvr_xr
    )
    target_include_directories(
        wawvr_xr_d3d9ex_bridge_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME xr.d3d9ex_bridge
        COMMAND wawvr_xr_d3d9ex_bridge_tests
    )
endif()
