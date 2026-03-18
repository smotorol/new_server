collect_sources(DC_CONTROL_SERVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_control_service ${DC_CONTROL_SERVICE_SOURCES})

dc_target_defaults(dc_control_service)
dc_target_src_root(dc_control_service)

target_link_libraries(dc_control_service PUBLIC
    dc_shared
    dc_core
    dc_net
    dc_service_runtime
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_CONTROL_SERVICE_SOURCES}")
