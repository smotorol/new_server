add_executable(sample_client main.cpp)
dc_target_defaults(sample_client)
dc_target_src_root(sample_client)
target_link_libraries(sample_client PRIVATE
    dc_core dc_net dc_app dc_proto
)
