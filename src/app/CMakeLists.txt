file(GLOB_RECURSE DC_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/endpoint/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/endpoint/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h
)

add_library(dc_app ${DC_APP_SOURCES})

dc_target_defaults(dc_app)
dc_target_src_root(dc_app)

target_link_libraries(dc_app PUBLIC
    dc_core
    dc_net
)
