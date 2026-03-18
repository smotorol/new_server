#pragma once

#include <cstdint>

#include "proto/common/types.h"
#include "proto/common/proto_base.h"

namespace proto::internal::control_world {

    enum class ControlWorldMsg : std::uint16_t
    {
        control_server_hello = 3101,
        control_server_register_ack = 3102,
    };

#pragma pack(push, 1)
    struct ControlServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
    };

    struct ControlServerRegisterAck
    {
        std::uint8_t accepted = 0;
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
    };
#pragma pack(pop)

    static_assert(sizeof(ControlServerHello) == 4 + 2 + (dc::k_service_name_max_len + 1));
    static_assert(sizeof(ControlServerRegisterAck) == 1 + 4 + 2 + (dc::k_service_name_max_len + 1));
}
