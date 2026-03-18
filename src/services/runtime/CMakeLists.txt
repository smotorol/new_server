collect_sources(DC_SERVICE_RUNTIME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_service_runtime ${DC_SERVICE_RUNTIME_SOURCES})

dc_target_defaults(dc_service_runtime)
dc_target_src_root(dc_service_runtime)

target_link_libraries(dc_service_runtime PUBLIC
    dc_shared
    dc_core
    dc_net
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_SERVICE_RUNTIME_SOURCES}")
