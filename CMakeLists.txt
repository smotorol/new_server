cmake_minimum_required(VERSION 3.24)
project(dc_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===============================
# Output directories (ALL -> Bin)
# ===============================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin)

# Visual Studio 같은 멀티-컨피그에서도 Debug/Release 폴더 안 만들게 강제
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${cfg}" cfg_u)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_u} ${CMAKE_SOURCE_DIR}/Bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_u} ${CMAKE_SOURCE_DIR}/Bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_u} ${CMAKE_SOURCE_DIR}/Bin)
endforeach()

# vcpkg가 설치한 패키지 찾기
find_package(Boost REQUIRED COMPONENTS system)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(nanodbc CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# redis-plus-plus 타겟명 호환 처리 (dynamic/static/버전 차이 대응)
set(REDISPP_TARGET "")

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

# ------------------------------------------------------------
# Helpers: collect sources + make VS filters from folder tree
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

# ---- 공용 라이브러리: common ----
collect_sources(COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/common)

add_library(common ${COMMON_SOURCES})
target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
  ${CMAKE_CURRENT_SOURCE_DIR}/src/db
  ${CMAKE_CURRENT_SOURCE_DIR}/external/inipp_repo
)
target_link_libraries(common PUBLIC Boost::system)
group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/common "src\\common" "${COMMON_SOURCES}")

# ---- 공용 라이브러리: net ----
collect_sources(NET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/net)

add_library(net ${NET_SOURCES})
target_include_directories(net PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/src/net
)
target_link_libraries(net PUBLIC
common Boost::system fmt::fmt spdlog::spdlog
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/net "src\\net" "${NET_SOURCES}")

# ---- 공용 라이브러리: dc_app (NetworkEXBase + EndpointApp) ----
collect_sources(APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

add_library(dc_app ${APP_SOURCES})
target_include_directories(dc_app PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app
  ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)
target_link_libraries(dc_app PUBLIC common net)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/app "src\\app" "${APP_SOURCES}")

# ---- DB (nanodbc + Windows ODBC) ----
add_library(db INTERFACE)
target_link_libraries(db INTERFACE nanodbc)

# Windows에서 ODBC 링크 보강 (환경에 따라 필요)
if (WIN32)
  target_link_libraries(db INTERFACE odbc32)
endif()

# DB 구현(풀) - 실제 컴파일되는 라이브러리
collect_sources(DB_IMPL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/db)

add_library(db_impl ${DB_IMPL_SOURCES})
target_link_libraries(db_impl PUBLIC
db ${REDISPP_TARGET} hiredis::hiredis
)
target_include_directories(db_impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/db)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/db "src\\db" "${DB_IMPL_SOURCES}")

# ---- Server ----
collect_sources(SERVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/server)

add_executable(server ${SERVER_SOURCES})
target_link_libraries(server PRIVATE
common net dc_app fmt::fmt spdlog::spdlog
${REDISPP_TARGET} hiredis::hiredis
db_impl
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/server "src\\server" "${SERVER_SOURCES}")

# ---- test_client ----
collect_sources(TEST_CLIENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/test_client)

add_executable(test_client ${TEST_CLIENT_SOURCES})
target_link_libraries(test_client PRIVATE
common net dc_app
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/test_client "src\\test_client" "${TEST_CLIENT_SOURCES}")

# ---- sample_server ----
collect_sources(SAMPLE_SERVER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_server)

add_executable(sample_server ${SAMPLE_SERVER_SOURCES})
target_link_libraries(sample_server PRIVATE
common net dc_app
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/sample_server "src\\sample_server" "${SAMPLE_SERVER_SOURCES}")

# ---- sample_client ----
collect_sources(SAMPLE_CLIENT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/sample_client)

add_executable(sample_client ${SAMPLE_CLIENT_SOURCES})
target_link_libraries(sample_client PRIVATE
common net dc_app
)

group_sources(${CMAKE_CURRENT_SOURCE_DIR}/src/sample_client "src\\sample_client" "${SAMPLE_CLIENT_SOURCES}")


#target_compile_options(common PUBLIC /utf-8)
target_compile_options(common PUBLIC /utf-8)
target_compile_options(db_impl PUBLIC /utf-8)
target_compile_options(net PUBLIC /utf-8)
target_compile_options(dc_app PUBLIC /utf-8)
target_compile_options(server PUBLIC /utf-8)
target_compile_options(test_client PUBLIC /utf-8)
target_compile_options(sample_server PUBLIC /utf-8)
target_compile_options(sample_client PUBLIC /utf-8)
#target_compile_options(test_client PUBLIC /utf-8)

if (MSVC)
  # UTF-8 소스
  add_compile_options(/utf-8)

  # ===== Debug 디버깅용: 최적화 끄기 + 디버그 심볼 =====
  # /Od : 최적화 사용 안 함
  # /Ob0: 인라인 확장 끔(조사식/스택 추적 도움)
  # /Zi : PDB 디버그 정보 생성
  add_compile_options(
    $<$<CONFIG:Debug>:/Od>
    $<$<CONFIG:Debug>:/Ob0>
    $<$<CONFIG:Debug>:/Zi>
  )

  # 링커 디버그 정보
  add_link_options($<$<CONFIG:Debug>:/DEBUG>)
endif()

set(SERVER_TARGETS
    server
)

foreach(tgt IN LISTS SERVER_TARGETS)
    if (TARGET ${tgt})
        set_target_properties(${tgt} PROPERTIES
            DEBUG_POSTFIX "_D"
            VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Bin
        )
    endif()
endforeach()
