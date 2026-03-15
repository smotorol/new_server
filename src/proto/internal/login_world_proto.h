#pragma once

#include <cstdint>

#include "proto/common/types.h"
#include "proto/common/proto_base.h"

namespace proto::internal::login_world {

    inline constexpr std::size_t k_login_session_max_len = 64;
    inline constexpr std::size_t k_world_token_max_len = 32;

    enum class LoginWorldMsg : std::uint16_t
    {
        login_server_hello = 3001,
        login_server_register_ack = 3002,

        login_auth_ticket_upsert = 3011,
        login_auth_ticket_upsert_ack = 3012,
    };

#pragma pack(push, 1)
    struct LoginServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[proto::k_service_name_max_len + 1]{};
    };

    struct LoginServerRegisterAck
    {
        std::uint8_t accepted = 0;
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[proto::k_service_name_max_len + 1]{};
    };

    struct LoginAuthTicketUpsert
    {
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint64_t expire_at_unix_sec = 0;
        char login_session[k_login_session_max_len + 1]{};
        char world_token[k_world_token_max_len + 1]{};
    };

    struct LoginAuthTicketUpsertAck
    {
        std::uint8_t accepted = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char world_token[k_world_token_max_len + 1]{};
    };
#pragma pack(pop)

} // namespace proto::internal
