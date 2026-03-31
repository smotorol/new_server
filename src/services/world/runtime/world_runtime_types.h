#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "services/world/common/character_core_state.h"
#include "services/world/runtime/world_session_types.h"

namespace svr {

	struct RemoteServiceLineState
	{
		bool registered = false;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint32_t server_id = 0;
		std::string server_name;
		std::uint16_t listen_port = 0;
	};

	struct ZoneRouteState
	{
		bool registered = false;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint32_t server_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t world_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t map_instance_capacity = 0;
		std::uint16_t active_map_instance_count = 0;
		std::uint16_t active_player_count = 0;
		std::uint16_t load_score = 0;
		std::uint32_t flags = 0;
		std::string server_name;
		std::vector<std::uint32_t> served_map_ids;
		std::chrono::steady_clock::time_point last_heartbeat_at{};
	};

	struct ZoneRouteInfo
	{
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint32_t zone_server_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t active_map_instance_count = 0;
		std::uint16_t active_player_count = 0;
		std::uint16_t load_score = 0;
		std::uint32_t flags = 0;
		std::vector<std::uint32_t> served_map_ids;
		std::chrono::steady_clock::time_point last_heartbeat_at{};
	};

	struct MapAssignmentEntry
	{
		std::uint32_t zone_server_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
	};

	struct PendingZoneAssignRequest
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint64_t map_key = 0;
		std::uint32_t target_sid = 0;
		std::uint32_t target_serial = 0;
		std::uint32_t target_zone_server_id = 0;
		std::uint16_t target_zone_id = 0;
		std::uint16_t target_channel_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::chrono::steady_clock::time_point issued_at{};
	};

	struct PendingEnterWorldFinalize
	{
		std::uint64_t assign_request_id = 0;
		PendingEnterWorldConsumeRequest enter_pending{};
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		CharacterCoreState core_state{};
	};

	struct PendingZonePlayerEnterRequest
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint32_t target_sid = 0;
		std::uint32_t target_serial = 0;
		std::uint32_t target_zone_server_id = 0;
		std::uint16_t target_zone_id = 0;
		PendingEnterWorldConsumeRequest enter_pending{};
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		CharacterCoreState core_state{};
		std::string cached_state_blob;
		std::chrono::steady_clock::time_point issued_at{};
	};

	struct PendingPortalTransfer
	{
		std::uint64_t trace_id = 0;
		std::uint64_t assign_request_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t char_id = 0;
		std::uint16_t source_zone_id = 0;
		std::uint32_t source_map_template_id = 0;
		std::uint32_t source_instance_id = 0;
		std::int32_t source_x = 0;
		std::int32_t source_y = 0;
		std::uint32_t target_map_template_id = 0;
		std::uint32_t target_instance_id = 0;
		std::int32_t target_x = 0;
		std::int32_t target_y = 0;
		std::chrono::steady_clock::time_point issued_at{};
	};

	struct PendingCharacterEnterSnapshotRequest
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		PendingEnterWorldConsumeRequest enter_pending{};
		std::chrono::steady_clock::time_point issued_at{};
	};

	struct LeaveWorldContext
	{
		std::uint64_t char_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint16_t zone_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
	};
} // namespace svr


