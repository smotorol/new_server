add_executable(world_server
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(world_server)
dc_target_src_root(world_server)
dc_set_vs_workdir(world_server)

target_link_libraries(world_server PRIVATE
    dc_shared
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    dc_db_odbc
    dc_server_common
    dc_world_service
)
