find_package(Boost REQUIRED COMPONENTS system)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# nlohmann_json을 이미 쓰거나 앞으로 쓸 가능성이 높으면 미리 둔다.
find_package(nlohmann_json CONFIG REQUIRED)

# ODBC
if(WIN32)
    # Windows는 system lib 직접 링크
else()
    find_package(ODBC REQUIRED)
endif()

# Redis++
set(REDISPP_TARGET "")
set(HIREDIS_TARGET "")

find_package(hiredis CONFIG QUIET)
find_package(redis++ CONFIG QUIET)

# redis++ 타겟명 호환 처리
if (TARGET redis++::redis++)
    set(REDISPP_TARGET redis++::redis++)
elseif (TARGET redis++::redis++_static)
    set(REDISPP_TARGET redis++::redis++_static)
elseif (TARGET redis++)
    set(REDISPP_TARGET redis++)
endif()

# hiredis 타겟명 호환 처리
if (TARGET hiredis::hiredis)
    set(HIREDIS_TARGET hiredis::hiredis)
elseif (TARGET hiredis)
    set(HIREDIS_TARGET hiredis)
endif()

if (REDISPP_TARGET STREQUAL "")
    message(FATAL_ERROR
        "redis++ imported target not found. "
        "Check vcpkg install and package target name.")
endif()

if (HIREDIS_TARGET STREQUAL "")
    message(FATAL_ERROR
        "hiredis imported target not found. "
        "Check vcpkg install and package target name.")
endif()

# inipp (header-only)
add_library(inipp INTERFACE)
target_include_directories(inipp INTERFACE
    ${CMAKE_SOURCE_DIR}/external/inipp_repo
)

if(NOT DEFINED DC_PROTOBUF_ROOT)
    if(DEFINED ENV{DC_PROTOBUF_ROOT} AND NOT "$ENV{DC_PROTOBUF_ROOT}" STREQUAL "")
        set(DC_PROTOBUF_ROOT "$ENV{DC_PROTOBUF_ROOT}" CACHE PATH "External protobuf toolchain root")
    else()
        set(DC_PROTOBUF_ROOT "" CACHE PATH "External protobuf toolchain root")
    endif()
endif()

option(ENABLE_PROTOBUF_CODEGEN "Enable protobuf code generation for gradual packet migration" ON)
set(DC_PROTOBUF_AVAILABLE OFF)

if(NOT DC_PROTOBUF_ROOT STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "${DC_PROTOBUF_ROOT}")
    list(PREPEND CMAKE_PREFIX_PATH "${DC_PROTOBUF_ROOT}/share/protobuf")
endif()

find_package(absl CONFIG QUIET)
find_package(utf8-range CONFIG QUIET)
find_package(Protobuf CONFIG QUIET)
if(Protobuf_FOUND)
    set(DC_PROTOBUF_AVAILABLE ON)
    if(NOT DC_PROTOBUF_ROOT STREQUAL "" AND EXISTS "${DC_PROTOBUF_ROOT}/tools/protobuf/protoc.exe")
        set(Protobuf_PROTOC_EXECUTABLE "${DC_PROTOBUF_ROOT}/tools/protobuf/protoc.exe" CACHE FILEPATH "protoc executable" FORCE)
    endif()
    message(STATUS "Protobuf found. root='${DC_PROTOBUF_ROOT}' protoc='${Protobuf_PROTOC_EXECUTABLE}'")
elseif(ENABLE_PROTOBUF_CODEGEN)
    message(WARNING "ENABLE_PROTOBUF_CODEGEN is ON but protobuf was not found. Generated first-path protobuf code will be disabled.")
endif()

