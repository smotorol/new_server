#pragma once

#include <cstdint>
#include "shared/constants.h"

namespace proto::login {

    enum class LoginC2SMsg : std::uint16_t
    {
        login_request = 1001,
        world_list_request = 1002,
        world_select_request = 1003,
        character_list_request = 1004,
        character_select_request = 1005,
    };

    enum class LoginS2CMsg : std::uint16_t
    {
        login_result = 1101,
        world_list_response = 1102,
        world_select_response = 1103,
        character_list_response = 1104,
        character_select_response = 1105,
    };

    enum class WorldSelectFailReason : std::uint16_t
    {
        success = 0,
        not_logged_in = 1,
        world_not_found = 2,
        world_not_selectable = 3,
        invalid_login_session = 4,
        internal_error = 5,
    };

    enum class CharacterSelectFailReason : std::uint16_t
    {
        success = 0,
        not_logged_in = 1,
        character_not_found = 2,
        character_not_selectable = 3,
        character_account_mismatch = 4,
        invalid_login_session = 5,
        world_not_ready = 6,
        internal_error = 7,
        world_not_selected = 8,
    };

#pragma pack(push, 1)
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

    struct C2S_login_request
    {
        char login_id[dc::k_login_id_max_len + 1]{};
        char password[dc::k_login_pw_max_len + 1]{};
    };

    struct S2C_login_result
    {
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t world_port = 0;
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_host[dc::k_world_host_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };

    struct C2S_world_list_request
    {
        std::uint8_t reserved = 0;
    };

    struct S2C_world_list_response
    {
        std::uint8_t ok = 0;
        std::uint16_t count = 0;
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
        WorldSummary worlds[dc::k_world_list_max_count]{};
    };

    struct C2S_world_select_request
    {
        std::uint16_t world_id = 0;
        std::uint16_t channel_id = 0;
    };

    struct S2C_world_select_response
    {
        std::uint8_t ok = 0;
        std::uint16_t fail_reason = static_cast<std::uint16_t>(WorldSelectFailReason::success);
        std::uint16_t world_id = 0;
        std::uint16_t world_port = 0;
        char world_host[dc::k_world_host_max_len + 1]{};
    };

    struct C2S_character_list_request
    {
        std::uint8_t reserved = 0;
    };

    struct S2C_character_list_response
    {
        std::uint8_t ok = 0;
        std::uint16_t count = 0;
        char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
        CharacterSummary characters[dc::k_character_list_max_count]{};
    };

    struct C2S_character_select_request
    {
        std::uint64_t char_id = 0;
    };

    struct S2C_character_select_response
    {
        std::uint8_t ok = 0;
        std::uint16_t fail_reason = static_cast<std::uint16_t>(CharacterSelectFailReason::success);
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t world_port = 0;
        char world_host[dc::k_world_host_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };
#pragma pack(pop)

} // namespace proto
