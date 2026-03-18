collect_sources(DC_NET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_net ${DC_NET_SOURCES})

dc_target_defaults(dc_net)
dc_target_src_root(dc_net)

target_link_libraries(dc_net PUBLIC
    dc_shared
    dc_core
    Boost::system
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_NET_SOURCES}")
