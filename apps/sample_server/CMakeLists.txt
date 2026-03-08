add_executable(sample_server main.cpp)
dc_target_defaults(sample_server)
dc_target_src_root(sample_server)
target_link_libraries(sample_server PRIVATE
    dc_core dc_net dc_app dc_proto
)
