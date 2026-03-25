add_executable(zone_data_builder
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(zone_data_builder)
dc_target_src_root(zone_data_builder)
dc_set_vs_workdir(zone_data_builder)

target_link_libraries(zone_data_builder PRIVATE
    dc_shared
    dc_core
    dc_server_common
)
