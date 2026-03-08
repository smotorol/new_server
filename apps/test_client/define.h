#pragma once

#include <type_traits>
#include <cstdint>

enum class eLine : std::uint32_t
{
    sample_server = 0,
    count,
};

constexpr std::size_t line_count()
{
    return static_cast<std::size_t>(eLine::count);
}

constexpr bool is_valid_line(eLine v)
{
    using U = std::underlying_type_t<eLine>;
    const auto uv = static_cast<U>(v);
    return uv >= 0 && static_cast<std::size_t>(uv) < line_count();
}

constexpr std::size_t to_index_checked(eLine v)
{
    // constexpr context에서도 쓸 수 있게 assert 대신 조건만 제공
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<eLine>>(v));
}
