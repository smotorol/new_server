collect_sources(TEST_CLIENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_client
    ${TEST_CLIENT_SOURCES}
)

dc_target_defaults(test_client)
dc_target_src_root(test_client)
dc_set_vs_workdir(test_client)

target_link_libraries(test_client PRIVATE
    dc_shared
    dc_core
    dc_net
    dc_app
    dc_proto
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${TEST_CLIENT_SOURCES}")
