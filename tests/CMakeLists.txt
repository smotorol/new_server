add_executable(world_regression_tests
    world_regression_tests.cpp
)

dc_target_defaults(world_regression_tests)
dc_target_src_root(world_regression_tests)
dc_set_vs_workdir(world_regression_tests)

target_link_libraries(world_regression_tests PRIVATE
    dc_core
    dc_proto
    dc_server_common
)

add_test(
    NAME world_regression_tests
    COMMAND world_regression_tests
)
