collect_sources(DC_LOGIN_SERVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_login_service ${DC_LOGIN_SERVICE_SOURCES})

dc_target_defaults(dc_login_service)
dc_target_src_root(dc_login_service)

target_link_libraries(dc_login_service PUBLIC
    dc_core
    dc_net
    dc_service_runtime
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_LOGIN_SERVICE_SOURCES}")

