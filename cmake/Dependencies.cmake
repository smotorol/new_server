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
