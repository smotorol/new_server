add_executable(control_server
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(control_server)
dc_target_src_root(control_server)
dc_set_vs_workdir(control_server)

target_link_libraries(control_server PRIVATE
    dc_shared
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    dc_db_odbc
    dc_server_common
    dc_control_service
)
