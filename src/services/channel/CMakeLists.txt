file(GLOB_RECURSE DC_CHANNEL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/handler/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/actors/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/actors/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/persistence/*.h
)

add_library(dc_channel_logic ${DC_CHANNEL_SOURCES})

dc_target_defaults(dc_channel_logic)
dc_target_src_root(dc_channel_logic)

target_link_libraries(dc_channel_logic PUBLIC
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    dc_cache_redis
    dc_server_common
    inipp
)
