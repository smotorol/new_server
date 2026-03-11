collect_sources(DC_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dc_core ${DC_CORE_SOURCES})

dc_target_defaults(dc_core)
dc_target_src_root(dc_core)

target_link_libraries(dc_core PUBLIC
    Boost::system
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR} "" "${DC_CORE_SOURCES}")
