cmake_minimum_required(VERSION 3.24)

project(new_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SAMPLES "Build sample targets" ON)
option(BUILD_LOGIN_SERVER "Build login server" ON)
option(BUILD_WORLD_SERVER "Build world server" ON)
option(BUILD_CHANNEL_SERVER_LEGACY "Build legacy channel_server wrapper" OFF)
option(BUILD_CONTROL_SERVER "Build control server" ON)
option(BUILD_COMMUNITY_SERVER "Build community server" ON)
option(BUILD_ACCOUNT_SERVER "Build account server" ON)
option(BUILD_ZONE_SERVER "Build zone server" ON)
option(BUILD_TESTS "Build lightweight regression tests" ON)

# Debug일 때만 postfix 붙이기 (Release는 postfix 없음)
set(CMAKE_DEBUG_POSTFIX "_d")

include(cmake/CompilerOptions.cmake)
include(cmake/OutputDirs.cmake)
include(cmake/Dependencies.cmake)
include(cmake/Helpers.cmake)

add_subdirectory(src/core)
add_subdirectory(src/net)
add_subdirectory(src/app)
add_subdirectory(src/proto)
add_subdirectory(src/db)
add_subdirectory(src/cache)
add_subdirectory(src/server_common)
add_subdirectory(src/services)
add_subdirectory(src/shared)

if(BUILD_CHANNEL_SERVER_LEGACY)
    add_subdirectory(apps/channel_server)
endif()

add_subdirectory(apps/test_client)

if(BUILD_LOGIN_SERVER)
    add_subdirectory(apps/login_server)
endif()

if(BUILD_WORLD_SERVER)
    add_subdirectory(apps/world_server)
endif()

if(BUILD_CONTROL_SERVER)
    add_subdirectory(apps/control_server)
endif()

if(BUILD_COMMUNITY_SERVER)
    add_subdirectory(apps/community_server)
endif()

if(BUILD_ACCOUNT_SERVER)
    add_subdirectory(apps/account_server)
endif()

if(BUILD_ZONE_SERVER)
    add_subdirectory(apps/zone_server)
endif()

if(BUILD_SAMPLES)
    add_subdirectory(apps/sample_server)
    add_subdirectory(apps/sample_client)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "VCPKG_ROOT ENV = $ENV{MY_VCPKG_ROOT}")
message(STATUS "CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")
