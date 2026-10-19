add_executable(wawvr-process-probe main.cpp)
target_link_libraries(wawvr-process-probe PRIVATE wawvr_project_options)
set_target_properties(wawvr-process-probe PROPERTIES OUTPUT_NAME "wawvr-process-probe")
