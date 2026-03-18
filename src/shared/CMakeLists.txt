add_library(dc_shared INTERFACE)

target_include_directories(dc_shared
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)
