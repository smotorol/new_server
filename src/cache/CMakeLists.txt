collect_sources(DC_CACHE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_cache_redis ${DC_CACHE_SOURCES})

dc_target_defaults(dc_cache_redis)
dc_target_src_root(dc_cache_redis)

target_link_libraries(dc_cache_redis PUBLIC
    dc_shared
    dc_core
    dc_db_core
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_CACHE_SOURCES}")

if(TARGET ${REDISPP_TARGET})
    target_link_libraries(dc_cache_redis PUBLIC ${REDISPP_TARGET})
endif()

if(TARGET ${HIREDIS_TARGET})
    target_link_libraries(dc_cache_redis PUBLIC ${HIREDIS_TARGET})
endif()
