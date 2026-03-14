#pragma once

#include <cstdint>

#include "proto/client/login_proto.h"
#include "proto/common/types.h"

namespace proto::internal {

    inline constexpr std::size_t k_service_name_max_len = 32;
    inline constexpr std::size_t k_auth_fail_reason_max_len = 64;

    enum class LoginAccountMsg : std::uint16_t
    {
        login_server_hello = 3201,
        login_server_register_ack = 3202,

        account_auth_request = 3211,
        account_auth_result = 3212,
    };

#pragma pack(push, 1)

    struct LoginServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[k_service_name_max_len + 1]{};
    };

    struct LoginServerRegisterAck
    {
        std::uint8_t accepted = 0;
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[k_service_name_max_len + 1]{};
    };

    struct AccountAuthRequest
    {
        std::uint64_t request_id = 0;
        std::uint64_t selected_char_id = 0;
        char login_id[proto::k_login_id_max_len + 1]{};
        char password[proto::k_login_pw_max_len + 1]{};
    };

    struct AccountAuthResult
    {
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char fail_reason[k_auth_fail_reason_max_len + 1]{};
    };

#pragma pack(pop)

} // namespace proto::internal
