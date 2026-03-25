add_executable(item_template_import
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

dc_target_defaults(item_template_import)
dc_target_src_root(item_template_import)
dc_set_vs_workdir(item_template_import)

target_link_libraries(item_template_import PRIVATE
    dc_shared
    dc_core
    dc_db_core
    dc_db_odbc
    dc_world_service
)
