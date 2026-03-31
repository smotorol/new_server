#pragma once
#include <cstddef>
#include <chrono>

namespace dc {
	inline constexpr std::size_t k_login_session_max_len = 64;
	inline constexpr std::size_t k_world_token_max_len = 32;
	inline constexpr std::size_t k_reconnect_token_max_len = 64;
	inline constexpr std::size_t k_login_id_max_len = 32;
	inline constexpr std::size_t k_login_pw_max_len = 64;
	inline constexpr std::size_t k_world_host_max_len = 64;
	inline constexpr std::size_t k_auth_fail_reason_max_len = 64;
	inline constexpr std::size_t k_character_name_max_len = 20;
	inline constexpr std::size_t k_character_list_max_count = 8;
	inline constexpr std::size_t k_world_list_max_count = 16;
	inline constexpr std::size_t k_character_state_blob_max_len = 128;
	inline constexpr std::size_t k_max_world_name_len = 32;
	inline constexpr std::size_t k_service_name_max_len = 32;
	inline constexpr std::size_t k_enter_trace_id_max_len = 32;

#ifdef _DEBUG
    inline constexpr std::chrono::seconds k_login_request_timeout{600};
    inline constexpr std::chrono::seconds k_account_world_expire{600};
	inline constexpr std::chrono::seconds k_pending_world_upserts_expire{ 600 };
	inline constexpr std::chrono::seconds k_consumed_world_enters_awaiting_notify_expire{ 600 };
	inline constexpr std::chrono::seconds kWorldRouteHeartbeatTimeout_{ 600 };
	inline constexpr std::chrono::seconds k_login_sessions_pending_client_sid{ 600 };
	inline constexpr std::chrono::seconds k_next_account_route_heartbeat_tp{ 3 };
	inline constexpr std::chrono::seconds k_next_stat_tp_{ 600 };
	inline constexpr std::chrono::seconds k_ExpireStaleZoneRoutes{ 600 };
	inline constexpr std::chrono::seconds k_pending_zone_player_enter_requests{ 600 };
	inline constexpr std::chrono::seconds k_pending_portal_transfer_requests{ 600 };
	inline constexpr std::chrono::seconds k_next_world_heartbeat_tp{ 2 };
	inline constexpr std::chrono::seconds k_next_reap_tp{ 10 };
	inline constexpr std::chrono::seconds k_ReapEmptyDungeonInstances_{ 30 };
	inline constexpr std::chrono::milliseconds k_portal_transfer_cooldown{ 500 };
#else
	inline constexpr std::chrono::seconds k_login_request_timeout{ 5 };
	inline constexpr std::chrono::seconds k_account_world_expire{ 30 };
	inline constexpr std::chrono::seconds k_pending_world_upserts_expire{ 30 };
	inline constexpr std::chrono::seconds k_consumed_world_enters_awaiting_notify_expire{ 30 };
	inline constexpr std::chrono::seconds kWorldRouteHeartbeatTimeout_{ 10 };
	inline constexpr std::chrono::seconds k_login_sessions_pending_client_sid{ 30 };
	inline constexpr std::chrono::seconds k_next_account_route_heartbeat_tp{ 3 };
	inline constexpr std::chrono::seconds k_next_stat_tp_{ 1 };
	inline constexpr std::chrono::seconds k_ExpireStaleZoneRoutes{ 8 };
	inline constexpr std::chrono::seconds k_pending_zone_player_enter_requests{ 3 };
	inline constexpr std::chrono::seconds k_pending_portal_transfer_requests{ 3 };
	inline constexpr std::chrono::seconds k_next_world_heartbeat_tp{ 2 };
	inline constexpr std::chrono::seconds k_next_reap_tp{ 10 };
	inline constexpr std::chrono::seconds k_ReapEmptyDungeonInstances_{ 30 };
	inline constexpr std::chrono::milliseconds k_portal_transfer_cooldown{ 750 };
#endif
};
