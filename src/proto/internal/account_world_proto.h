#pragma once

#include <cstdint>

#include "proto/client/login_proto.h"
#include "proto/common/types.h"
#include "proto/common/proto_base.h"

namespace pt_l = proto::login;

namespace proto::internal::account_world {

    enum class AccountWorldMsg : std::uint16_t
    {
        world_server_hello = 3301,
        world_server_register_ack = 3302,
    };

#pragma pack(push, 1)

    struct WorldServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t public_port = 0;
        char server_name[proto::k_service_name_max_len + 1]{};
        char public_host[pt_l::k_world_host_max_len + 1]{};
    };

    struct WorldServerRegisterAck
    {
        std::uint8_t accepted = 0;
        std::uint32_t server_id = 0;
        std::uint16_t public_port = 0;
        char server_name[proto::k_service_name_max_len + 1]{};
        char public_host[pt_l::k_world_host_max_len + 1]{};
    };

#pragma pack(pop)

} // namespace proto::internal::account_world
