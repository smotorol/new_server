#pragma once

#include <cstdint>

#include "proto/common/types.h"

namespace proto::internal {

    inline constexpr std::size_t k_service_name_max_len = 32;

    enum class ControlWorldMsg : std::uint16_t
    {
        control_server_hello = 3101,
    };
#pragma pack(push, 1)
    struct ControlServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[k_service_name_max_len + 1];

        ControlServerHello()
        {
            proto::zero(*this);
        }
    };
#pragma pack(push, 1)
    static_assert(sizeof(ControlServerHello) == 4 + 2 + (k_service_name_max_len + 1));
}
