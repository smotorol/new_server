collect_sources(DC_APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_app ${DC_APP_SOURCES})

dc_target_defaults(dc_app)
dc_target_src_root(dc_app)

target_link_libraries(dc_app PUBLIC
    dc_core
    dc_net
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_APP_SOURCES}")
