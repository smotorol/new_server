collect_sources(DC_ACCOUNT_SERVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_account_service ${DC_ACCOUNT_SERVICE_SOURCES})

dc_target_defaults(dc_account_service)
dc_target_src_root(dc_account_service)

target_link_libraries(dc_account_service PUBLIC
    dc_core
    dc_net
    dc_service_runtime
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_ACCOUNT_SERVICE_SOURCES}")
