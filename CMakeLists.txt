cmake_minimum_required(VERSION 3.24)
project(dc_server LANGUAGES CXX)

option(BUILD_SERVER "Build server target" ON)
option(BUILD_TEST_CLIENT "Build test_client target" ON)
option(BUILD_SAMPLES "Build sample_client/sample_server targets" OFF)

# ===============================
# Global C++ settings
# ===============================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug일 때만 postfix 붙이기 (Release는 postfix 없음)
# 예) common_d.lib / server_d.exe (Debug), common.lib / server.exe (Release)
set(CMAKE_DEBUG_POSTFIX "_d")

# ===============================
# Output directories
#   - Debug/Release 산출물 섞이지 않게 분리
#   - VS 멀티-컨피그 / Ninja 싱글-컨피그 모두 안전
# ===============================
set(_BIN_BASE "${CMAKE_SOURCE_DIR}/Bin")

# 멀티-컨피그(Visual Studio)용: Bin/Debug, Bin/Release...
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${cfg}" cfg_u)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_u} "${_BIN_BASE}/${cfg}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_u} "${_BIN_BASE}/${cfg}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_u} "${_BIN_BASE}/${cfg}")
endforeach()

# 싱글-컨피그(Ninja/Makefiles)용 기본값: Bin/<CMAKE_BUILD_TYPE> (없으면 Bin)
if(NOT CMAKE_CONFIGURATION_TYPES)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_BIN_BASE}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_BIN_BASE}/${CMAKE_BUILD_TYPE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_BIN_BASE}/${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_BIN_BASE}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_BIN_BASE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_BIN_BASE}")
  endif()
endif()

# ===============================
# Dependencies (vcpkg)
# ===============================
find_package(Boost CONFIG QUIET COMPONENTS system)
if(NOT Boost_FOUND)
  find_package(Boost REQUIRED COMPONENTS system)
endif()
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(REDISPP_TARGET "")
if (BUILD_SERVER)
  find_package(hiredis CONFIG REQUIRED)
  find_package(redis++ CONFIG REQUIRED)

  # redis-plus-plus 타겟명 호환 처리 (dynamic/static/버전 차이 대응)
  if (TARGET redis++::redis++)
    set(REDISPP_TARGET redis++::redis++)
  elseif (TARGET redis++::redis++_static)
    set(REDISPP_TARGET redis++::redis++_static)
  elseif (TARGET redis++)
    set(REDISPP_TARGET redis++)
  else()
    message(FATAL_ERROR
      "redis++ package found but no known imported target exists. "
      "Hint: vcpkg says for static triplet use redis++::redis++_static.")
  endif()
endif()

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------
function(collect_sources out_var base_dir)
  file(GLOB_RECURSE _src CONFIGURE_DEPENDS
    ${base_dir}/*.cpp
    ${base_dir}/*.c
    ${base_dir}/*.h
    ${base_dir}/*.hpp
    ${base_dir}/*.inl
  )
  set(${out_var} ${_src} PARENT_SCOPE)
endfunction()

function(group_sources base_dir prefix files)
  if (MSVC)
    # Visual Studio 솔루션 필터를 실제 폴더 트리처럼 보여줌
    source_group(TREE ${base_dir} PREFIX "${prefix}" FILES ${files})
  endif()
endfunction()

function(enable_utf8 target_name)
  if (MSVC)
    target_compile_options(${target_name} PRIVATE /utf-8)
  endif()
endfunction()

function(set_vs_workdir target_name)
  if (MSVC)
    # VS 디버거 작업 디렉토리: 설정별 Bin/<cfg>
    set_target_properties(${target_name} PROPERTIES
      VS_DEBUGGER_WORKING_DIRECTORY "${_BIN_BASE}/$<CONFIG>"
    )
  endif()
endfunction()

# ===============================
# Libraries
# ===============================

# ---- common ----
collect_sources(COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/common)

add_library(common ${COMMON_SOURCES})
target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
  ${CMAKE_CURRENT_SOURCE_DIR}/src/db
  ${CMAKE_CURRENT_SOURCE_DIR}/external/inipp_repo
)
target_link_libraries(common PUBLIC Boost::system)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/common "src\\common" "${COMMON_SOURCES}")
enable_utf8(common)

# ---- net ----
collect_sources(NET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net)

add_library(net ${NET_SOURCES})
target_include_directories(net PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)
target_link_libraries(net PUBLIC
  common Boost::system fmt::fmt spdlog::spdlog
)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/net "src\\net" "${NET_SOURCES}")
enable_utf8(net)

# ---- dc_app (NetworkEXBase + EndpointApp) ----
collect_sources(APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

add_library(dc_app ${APP_SOURCES})
target_include_directories(dc_app PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)
target_link_libraries(dc_app PUBLIC common net)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/app "src\\app" "${APP_SOURCES}")
enable_utf8(dc_app)

# ---- DB (direct ODBC wrapper) ----
add_library(db INTERFACE)
if (WIN32)
  target_link_libraries(db INTERFACE odbc32)
else()
  find_package(ODBC REQUIRED)
  target_link_libraries(db INTERFACE ODBC::ODBC)
endif()

# ---- DB impl (pool, redis, etc.) ----
collect_sources(DB_IMPL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/db)

add_library(db_impl ${DB_IMPL_SOURCES})
target_link_libraries(db_impl PUBLIC
  db ${REDISPP_TARGET} hiredis::hiredis
)
target_include_directories(db_impl PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/db
)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/db "src\\db" "${DB_IMPL_SOURCES}")
enable_utf8(db_impl)

# ===============================
# Executables
# ===============================

# ---- server ----
collect_sources(SERVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/server)

add_executable(server ${SERVER_SOURCES})
target_link_libraries(server PRIVATE
  common net dc_app fmt::fmt spdlog::spdlog
  ${REDISPP_TARGET} hiredis::hiredis
  db_impl
)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/server "src\\server" "${SERVER_SOURCES}")
enable_utf8(server)
set_vs_workdir(server)

# ---- test_client ----
collect_sources(TEST_CLIENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/test_client)

add_executable(test_client ${TEST_CLIENT_SOURCES})
target_link_libraries(test_client PRIVATE
  common net dc_app
)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/test_client "src\\test_client" "${TEST_CLIENT_SOURCES}")
enable_utf8(test_client)
set_vs_workdir(test_client)

# ---- sample_server ----
collect_sources(SAMPLE_SERVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_server)

add_executable(sample_server ${SAMPLE_SERVER_SOURCES})
target_link_libraries(sample_server PRIVATE
  common net dc_app
)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/sample_server "src\\sample_server" "${SAMPLE_SERVER_SOURCES}")
enable_utf8(sample_server)
set_vs_workdir(sample_server)

# ---- sample_client ----
collect_sources(SAMPLE_CLIENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_client)

add_executable(sample_client ${SAMPLE_CLIENT_SOURCES})
target_link_libraries(sample_client PRIVATE
  common net dc_app
)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/sample_client "src\\sample_client" "${SAMPLE_CLIENT_SOURCES}")
enable_utf8(sample_client)
set_vs_workdir(sample_client)

# ===============================
# MSVC debug-friendly options
# ===============================
if (MSVC)
  # Debug: 최적화 끄기 + 조사에 유리한 설정
  add_compile_options(
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Debug>:/Ob0>
    $<$<CONFIG:Debug>:/Zi>
  )
  add_link_options($<$<CONFIG:Debug>:/DEBUG>)
endif()
