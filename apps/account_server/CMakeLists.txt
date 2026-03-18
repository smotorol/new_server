add_executable(account_server
    main.cpp
)

dc_target_defaults(account_server)
dc_target_src_root(account_server)

target_link_libraries(account_server PRIVATE
    dc_shared
    dc_core
    dc_account_service
)
