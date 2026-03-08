#pragma once
#include <cstdint>
#include <array>
#include <cstring>

namespace proto {
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i32 = std::int32_t;

    // 레거시 _UNICHAR 대체(2바이트 문자)
    using unichar = char16_t;

    template <typename T>
    inline void zero(T& v) { std::memset(&v, 0, sizeof(T)); }
}
