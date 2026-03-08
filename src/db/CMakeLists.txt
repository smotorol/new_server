add_library(dc_db_core INTERFACE)
target_include_directories(dc_db_core INTERFACE
    ${CMAKE_SOURCE_DIR}/src
)

add_library(dc_db_odbc
    ${CMAKE_CURRENT_SOURCE_DIR}/odbc/odbc_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/odbc/odbc_wrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/shard/db_shard_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shard/db_shard_manager.h
)

dc_target_defaults(dc_db_odbc)
dc_target_src_root(dc_db_odbc)

target_link_libraries(dc_db_odbc PUBLIC
    dc_core
    dc_db_core
)

if(WIN32)
    target_link_libraries(dc_db_odbc PUBLIC odbc32)
else()
    target_link_libraries(dc_db_odbc PUBLIC ODBC::ODBC)
endif()
