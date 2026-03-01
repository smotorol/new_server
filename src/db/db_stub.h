#pragma once
// nanodbc 기반 DB 모듈 스텁(프로젝트 빌드용).
// 실제 구현은 여기에 확장하면 됨.
#include <nanodbc/nanodbc.h>
#include <string>

namespace db {

inline nanodbc::connection connect_dsn(const std::string& dsn)
{
    // 예: "DSN=DC_ACCOUNT;UID=sa;PWD=Strong!Pass123;"
    return nanodbc::connection(dsn);
}

} // namespace db
