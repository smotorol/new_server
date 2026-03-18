collect_sources(DC_ZONE_SERVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_zone_service ${DC_ZONE_SERVICE_SOURCES})

dc_target_defaults(dc_zone_service)
dc_target_src_root(dc_zone_service)

target_link_libraries(dc_zone_service PUBLIC
    dc_shared
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_server_common
    dc_service_runtime
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_ZONE_SERVICE_SOURCES}")
