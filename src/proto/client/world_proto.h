#pragma once

#include <cstdint>
#include "shared/constants.h"

namespace proto::world {

    enum class WorldC2SMsg : std::uint16_t
    {
        enter_world_with_token = 2001,
    };

    enum class WorldS2CMsg : std::uint16_t
    {
        enter_world_result = 2101,
        kick_notify = 2102,
    };

    enum class WorldKickReason : std::uint16_t
    {
        unknown = 0,
        duplicate_login = 1,
        server_shutdown = 2,
        admin_kick = 3,
    };

    enum class EnterWorldResultCode : std::uint16_t
    {
        success = 0,
        invalid_packet = 1,
        auth_ticket_not_found = 2,
        auth_ticket_expired = 3,
        auth_ticket_replayed = 4,
        auth_ticket_mismatch = 5,
        bind_invalid_input = 6,
        internal_error = 7,
        zone_not_available = 8,
        zone_assign_send_failed = 9,
        zone_assign_timeout = 10,
        zone_assign_route_lost = 11,
        zone_assign_rejected = 12,
        zone_map_not_found = 13,
        zone_map_capacity_full = 14,
        zone_player_enter_send_failed = 15,
        account_enter_notify_failed = 16,
        zone_player_enter_timeout = 17,
        zone_player_enter_route_lost = 18,
        zone_player_enter_rejected = 19,
        zone_player_map_not_found = 20,
        enter_already_pending = 21,
        already_in_world = 22,
        session_closing = 23,
    };

#pragma pack(push, 1)
    struct C2S_enter_world_with_token
    {
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0; // deprecated: server ignores client-provided char_id
        char login_session[dc::k_login_session_max_len + 1]{};
        char world_token[dc::k_world_token_max_len + 1]{};
    };

    struct S2C_enter_world_result
	{
		std::uint8_t ok = 0;
		std::uint16_t reason = static_cast<std::uint16_t>(EnterWorldResultCode::success);
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
    };

    struct S2C_kick_notify
    {
        std::uint16_t reason = static_cast<std::uint16_t>(WorldKickReason::unknown);
        std::uint64_t char_id = 0;
    };
#pragma pack(pop)

} // namespace proto
