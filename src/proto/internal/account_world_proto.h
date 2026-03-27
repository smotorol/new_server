#pragma once

#include <cstdint>

#include "proto/common/types.h"
#include "proto/common/proto_base.h"
#include "shared/constants.h"

namespace proto::internal::account_world {

    enum : std::uint32_t
    {
        k_world_flag_accepting_players = 1u << 0,
        k_world_flag_visible = 1u << 1,
    };

    enum class AccountWorldMsg : std::uint16_t
    {
        world_server_hello = 3301,
        world_server_register_ack = 3302,
        world_server_route_heartbeat = 3303,

        world_auth_ticket_consume_request = 3311,
        world_auth_ticket_consume_response = 3312,

        world_enter_success_notify = 3313,
        world_character_list_request = 3314,
        world_character_list_response = 3315,
    };

#pragma pack(push, 1)

    struct WorldServerHello
    {
        std::uint32_t server_id = 0;
        std::uint16_t public_port = 0;
        std::uint16_t world_id = 0;
        std::uint16_t channel_id = 0;
        std::uint16_t active_zone_count = 0;
        std::uint16_t load_score = 0;
        std::uint32_t flags = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
        char public_host[dc::k_world_host_max_len + 1]{};
    };

    struct WorldServerRouteHeartbeat
    {
        std::uint32_t server_id = 0;
        std::uint16_t world_id = 0;
        std::uint16_t channel_id = 0;
        std::uint16_t active_zone_count = 0;
        std::uint16_t load_score = 0;
        std::uint32_t flags = 0;
    };

    struct WorldServerRegisterAck
    {
        std::uint8_t accepted = 0;
        std::uint32_t server_id = 0;
        std::uint16_t public_port = 0;
        std::uint16_t world_id = 0;
        std::uint16_t channel_id = 0;
        std::uint16_t active_zone_count = 0;
        std::uint16_t load_score = 0;
        std::uint32_t flags = 0;
        char server_name[dc::k_service_name_max_len + 1]{};
        char public_host[dc::k_world_host_max_len + 1]{};
    };

    struct WorldAuthTicketConsumeRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };

    struct WorldAuthTicketConsumeResponse
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint16_t result_code = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };

    struct WorldEnterSuccessNotify
    {
        std::uint64_t trace_id = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };

    struct WorldCharacterSummary
    {
        std::uint64_t char_id = 0;
        char char_name[dc::k_character_name_max_len + 1]{};
        std::uint32_t level = 0;
        std::uint16_t job = 0;
        std::uint32_t appearance_code = 0;
        std::uint64_t last_login_at_epoch_sec = 0;
    };

    struct WorldCharacterListRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        std::uint16_t world_id = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
    };

    struct WorldCharacterListResponse
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint16_t world_id = 0;
        std::uint16_t count = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
        WorldCharacterSummary characters[dc::k_character_list_max_count]{};
    };

#pragma pack(pop)

} // namespace proto::internal::account_world
