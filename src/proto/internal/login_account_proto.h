#pragma once

#include <cstdint>

#include "proto/client/login_proto.h"
#include "proto/common/types.h"
#include "proto/common/proto_base.h"

namespace pt_l = proto::login;

namespace proto::internal::login_account {

    inline constexpr std::size_t k_auth_fail_reason_max_len = 64;
    inline constexpr std::size_t k_login_session_max_len = 64;
    inline constexpr std::size_t k_world_token_max_len = 32;

    enum class LoginAccountMsg : std::uint16_t
    {
        login_server_hello = 3201,
        login_server_register_ack = 3202,

        account_auth_request = 3211,
        account_auth_result = 3212,

        world_enter_success_notify = 3213,
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

    struct AccountAuthRequest
    {
        std::uint64_t request_id = 0;
        std::uint64_t selected_char_id = 0;
        char login_id[pt_l::k_login_id_max_len + 1]{};
        char password[pt_l::k_login_pw_max_len + 1]{};
    };

    struct AccountAuthResult
    {
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t world_port = 0;
        char login_session[k_login_session_max_len + 1]{};
        char world_token[k_world_token_max_len + 1]{};
        char fail_reason[k_auth_fail_reason_max_len + 1]{};
        char world_host[pt_l::k_world_host_max_len + 1]{};
    };

    struct WorldEnterSuccessNotify
    {
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[k_login_session_max_len + 1]{};
        char world_token[k_world_token_max_len + 1]{};
    };

#pragma pack(pop)

} // namespace proto::internal::login_account
