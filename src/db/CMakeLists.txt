add_library(dc_db_core INTERFACE)
target_include_directories(dc_db_core INTERFACE
    ${CMAKE_SOURCE_DIR}/src
)

collect_sources(DC_DB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_db_odbc ${DC_DB_SOURCES})

dc_target_defaults(dc_db_odbc)
dc_target_src_root(dc_db_odbc)

target_link_libraries(dc_db_odbc PUBLIC
    dc_core
    dc_db_core
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_DB_SOURCES}")

if(WIN32)
    target_link_libraries(dc_db_odbc PUBLIC odbc32)
else()
    target_link_libraries(dc_db_odbc PUBLIC ODBC::ODBC)
endif()
