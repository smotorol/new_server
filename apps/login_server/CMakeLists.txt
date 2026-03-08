add_executable(login_server
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(login_server)
dc_target_src_root(login_server)
dc_set_vs_workdir(login_server)

target_link_libraries(login_server PRIVATE
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    dc_db_odbc
    dc_server_common
    dc_login_service
)
