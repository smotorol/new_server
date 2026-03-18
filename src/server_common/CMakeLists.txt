collect_sources(DC_SERVER_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_server_common ${DC_SERVER_COMMON_SOURCES})

dc_target_defaults(dc_server_common)
dc_target_src_root(dc_server_common)

target_link_libraries(dc_server_common PUBLIC
    dc_shared
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    inipp
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_SERVER_COMMON_SOURCES}")
