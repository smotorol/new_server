add_executable(zone_server main.cpp)

dc_target_defaults(zone_server)
dc_target_src_root(zone_server)
dc_set_vs_workdir(zone_server)

target_link_libraries(zone_server PRIVATE
    dc_shared
    dc_core
    dc_zone_service
)

if (MSVC)
    target_compile_options(zone_server PRIVATE /FS)
endif()
