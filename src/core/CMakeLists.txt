file(GLOB_RECURSE DC_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/log/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/log/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/util/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/util/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/*.h
)

add_library(dc_core ${DC_CORE_SOURCES})

dc_target_defaults(dc_core)
dc_target_src_root(dc_core)

target_link_libraries(dc_core PUBLIC
    Boost::system
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
