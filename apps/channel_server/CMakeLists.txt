add_executable(channel_server
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(channel_server)
dc_target_src_root(channel_server)
dc_set_vs_workdir(channel_server)

target_link_libraries(channel_server PRIVATE
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    dc_db_odbc
    dc_cache_redis
    dc_server_common
    dc_world_service
)
