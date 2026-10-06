add_library(
    wawvr_gameplay
    STATIC
        manual_reload_logic.cpp
        manual_reload_logic.hpp
        tracked_attachment_math.cpp
        tracked_attachment_math.hpp
)

target_link_libraries(wawvr_gameplay PUBLIC wawvr_project_options)
target_include_directories(
    wawvr_gameplay
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(WAWVR_BUILD_TESTS AND BUILD_TESTING)
    add_executable(
        WorldAtWarVRManualReloadLogicTest
        manual_reload_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRManualReloadLogicTest
        PRIVATE
            wawvr_gameplay
            wawvr_project_options
    )
    add_test(
        NAME gameplay.manual_reload_logic
        COMMAND WorldAtWarVRManualReloadLogicTest
    )

    add_executable(
        WorldAtWarVRTrackedAttachmentMathTest
        tracked_attachment_math_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRTrackedAttachmentMathTest
        PRIVATE
            wawvr_gameplay
            wawvr_project_options
    )
    add_test(
        NAME gameplay.tracked_attachment_math
        COMMAND WorldAtWarVRTrackedAttachmentMathTest
    )
endif()
