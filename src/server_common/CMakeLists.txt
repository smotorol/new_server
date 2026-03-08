add_library(dc_server_common INTERFACE)

target_include_directories(dc_server_common INTERFACE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dc_server_common INTERFACE
    dc_core
    dc_net
    dc_app
    dc_proto
    dc_db_core
    inipp
)
