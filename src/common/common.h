#pragma once
#include <spdlog/spdlog.h>
#include <string_view>

namespace common {

// 간단 래퍼: 프로젝트 전체에서 공용으로 로거 초기화/사용
inline void init_logging()
{
    // 필요하면 패턴/싱크/파일로거 등을 여기서 확장
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
}

inline void log_info(std::string_view msg) { spdlog::info("{}", msg); }
inline void log_warn(std::string_view msg) { spdlog::warn("{}", msg); }
inline void log_error(std::string_view msg){ spdlog::error("{}", msg); }

} // namespace common
