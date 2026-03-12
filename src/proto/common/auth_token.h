#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace proto {

    inline constexpr std::size_t k_auth_token_size = 32;

#pragma pack(push, 1)
    struct AuthToken
    {
        std::array<char, k_auth_token_size + 1> value{};

        std::string_view view() const noexcept
        {
            return std::string_view(value.data());
        }
    };
#pragma pack(pop)

} // namespace proto
