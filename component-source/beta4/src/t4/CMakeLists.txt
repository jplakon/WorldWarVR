add_library(
    wawvr_t4
    STATIC
        address.hpp
        bindings.cpp
        bindings.hpp
        hook_api.cpp
        hook_api.hpp
        pe_image.cpp
        pe_image.hpp
        profile.cpp
        profile.hpp
        runtime_win32.cpp
        runtime_win32.hpp
        sha256.cpp
        sha256.hpp
        usercmd.cpp
        usercmd.hpp
        validation.cpp
        validation.hpp
)

target_include_directories(wawvr_t4 PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(wawvr_t4 PUBLIC wawvr_project_options PRIVATE Psapi)

if(WAWVR_BUILD_TESTS AND BUILD_TESTING)
    add_executable(t4_profile_tests tests/profile_validation_tests.cpp)
    target_link_libraries(t4_profile_tests PRIVATE wawvr_t4 wawvr_project_options)
    add_test(NAME t4.profile_validation COMMAND t4_profile_tests)
endif()
