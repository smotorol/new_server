add_executable(community_server
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(community_server)
dc_target_src_root(community_server)
dc_set_vs_workdir(community_server)

target_link_libraries(community_server PRIVATE
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    dc_db_odbc
    dc_server_common
    dc_login_service
)
