#pragma once

#include <cstdint>

#include "proto/client/login_proto.h"
#include "proto/common/types.h"
#include "proto/common/proto_base.h"

namespace pt_l = proto::login;

namespace proto::internal::account_world {

    inline constexpr std::size_t k_login_session_max_len = 64;
    inline constexpr std::size_t k_world_token_max_len = 32;

    enum class AccountWorldMsg : std::uint16_t
    {
        world_server_hello = 3301,
        world_server_register_ack = 3302,

        world_auth_ticket_consume_request = 3311,
        world_auth_ticket_consume_response = 3312,

        world_enter_success_notify = 3313,
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

    struct WorldAuthTicketConsumeRequest
    {
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[k_login_session_max_len + 1]{};
        char world_token[k_world_token_max_len + 1]{};
    };

    struct WorldAuthTicketConsumeResponse
    {
        std::uint64_t request_id = 0;
        std::uint16_t result_code = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[k_login_session_max_len + 1]{};
        char world_token[k_world_token_max_len + 1]{};
    };

    struct WorldEnterSuccessNotify
    {
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[k_login_session_max_len + 1]{};
        char world_token[k_world_token_max_len + 1]{};
    };

#pragma pack(pop)

} // namespace proto::internal::account_world
