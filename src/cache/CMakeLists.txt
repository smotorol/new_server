add_library(dc_cache_redis
    ${CMAKE_CURRENT_SOURCE_DIR}/redis/redis_cache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/redis/redis_cache.h
)

dc_target_defaults(dc_cache_redis)
dc_target_src_root(dc_cache_redis)

target_link_libraries(dc_cache_redis PUBLIC
    dc_core
    dc_db_core
)

if(TARGET ${REDISPP_TARGET})
    target_link_libraries(dc_cache_redis PUBLIC ${REDISPP_TARGET})
endif()

if(TARGET ${HIREDIS_TARGET})
    target_link_libraries(dc_cache_redis PUBLIC ${HIREDIS_TARGET})
endif()
