file(GLOB_RECURSE DC_NET_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/actor/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/actor/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/packet/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/packet/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/*.h
)

add_library(dc_net ${DC_NET_SOURCES})

dc_target_defaults(dc_net)
dc_target_src_root(dc_net)

target_link_libraries(dc_net PUBLIC
    dc_core
    Boost::system
)
