#pragma once

#include <cstdint>

#include "proto/common/types.h"
#include "proto/common/proto_base.h"
#include "shared/constants.h"

namespace proto::internal::login_account {

    enum class Msg : std::uint16_t
    {
        login_server_hello = 3201,
        login_server_register_ack = 3202,

        account_auth_request = 3211,
        account_auth_result = 3212,
        world_list_request = 3213,
        world_list_response = 3214,
        world_select_request = 3215,
        world_select_response = 3216,
        character_list_request = 3217,
        character_list_response = 3218,
        character_select_request = 3219,
        character_select_response = 3220,

        world_enter_success_notify = 3221,
    };

#pragma pack(push, 1)

    struct LoginServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
    };

    struct LoginServerRegisterAck
    {
        std::uint8_t accepted = 0;
        std::uint32_t server_id = 0;
        std::uint16_t listen_port = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
    };

    struct WorldSummary
    {
        std::uint16_t world_id = 0;
        std::uint16_t channel_id = 0;
        std::uint16_t active_zone_count = 0;
        std::uint16_t load_score = 0;
        std::uint16_t public_port = 0;
        std::uint32_t flags = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
        char public_host[dc::k_world_host_max_len + 1]{};
    };

    struct CharacterSummary
    {
        std::uint64_t char_id = 0;
        char char_name[dc::k_character_name_max_len + 1]{};
        std::uint32_t level = 0;
        std::uint16_t job = 0;
        std::uint32_t appearance_code = 0;
        std::uint64_t last_login_at_epoch_sec = 0;
    };

    struct AccountAuthRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        char login_id[dc::k_login_id_max_len + 1]{};
        char password[dc::k_login_pw_max_len + 1]{};
    };

    struct AccountAuthResult
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t world_port = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
        char world_host[dc::k_world_host_max_len + 1]{};
    };

    struct WorldListRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
    };

    struct WorldListResponse
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint16_t count = 0;
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
        WorldSummary worlds[dc::k_world_list_max_count]{};
    };

    struct WorldSelectRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        std::uint16_t world_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
    };

    struct WorldSelectResponse
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint16_t world_id = 0;
        std::uint16_t world_port = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_host[dc::k_world_host_max_len + 1]{};
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
    };

    struct CharacterListRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        std::uint16_t world_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
    };

    struct CharacterListResponse
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint16_t world_id = 0;
        std::uint16_t count = 0;
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
        CharacterSummary characters[dc::k_character_list_max_count]{};
    };

    struct CharacterSelectRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
    };

    struct CharacterSelectResponse
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t world_port = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
        char world_host[dc::k_world_host_max_len + 1]{};
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
    };

    struct WorldEnterSuccessNotify
    {
        std::uint64_t trace_id = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };

#pragma pack(pop)

} // namespace proto::internal::login_account
