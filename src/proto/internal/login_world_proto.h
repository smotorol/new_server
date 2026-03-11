#pragma once

#include <cstdint>

#include "proto/common/types.h"

namespace proto::internal {

    inline constexpr std::size_t k_service_name_max_len = 32;

    enum class LoginWorldMsg : std::uint16_t
    {
        login_server_hello = 3001,
    };
#pragma pack(push, 1)
    struct LoginServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[k_service_name_max_len + 1];

        LoginServerHello()
        {
            proto::zero(*this);
        }
    };
#pragma pack(pop)
    static_assert(sizeof(LoginServerHello) == 4 + 2 + (k_service_name_max_len + 1));
}
