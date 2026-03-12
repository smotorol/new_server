#pragma once

#include <cstdint>

namespace proto {

    enum class LoginC2SMsg : std::uint16_t
    {
        login_request = 1001,
    };

    enum class LoginS2CMsg : std::uint16_t
    {
        login_result = 1101,
    };

    inline constexpr std::size_t k_login_id_max_len = 32;
    inline constexpr std::size_t k_login_pw_max_len = 64;
    inline constexpr std::size_t k_world_host_max_len = 64;

#pragma pack(push, 1)
    struct C2S_login_request
    {
        char login_id[k_login_id_max_len + 1]{};
        char password[k_login_pw_max_len + 1]{};
        std::uint64_t selected_char_id = 0;
    };

    struct S2C_login_result
    {
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t world_port = 0;
        char world_host[k_world_host_max_len + 1]{};
        char world_token[33]{};
    };
#pragma pack(pop)

} // namespace proto
