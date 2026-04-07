#pragma once

#include <cstdint>

#include "proto/common/proto_base.h"
#include "shared/constants.h"

namespace proto::internal::world_zone {

	enum class Msg : std::uint16_t
	{
		zone_server_hello = 3401,
		zone_server_register_ack = 3402,
		zone_server_route_heartbeat = 3403,

        world_zone_map_assign_request = 3411,
        zone_world_map_assign_response = 3412,
		world_zone_player_enter = 3413,
		world_zone_player_leave = 3414,
		zone_world_player_enter_ack = 3415,
		world_zone_player_move_internal = 3416,
		zone_world_aoi_snapshot = 3417,
		zone_world_aoi_spawn_batch = 3418,
		zone_world_aoi_despawn_batch = 3419,
		zone_world_aoi_move_batch = 3420,
	};

	enum class ZonePlayerEnterResultCode : std::uint16_t
	{
		ok = 0,
		map_not_found = 1,
		internal_error = 2,
 	};

	enum class ZoneMapAssignResultCode : std::uint16_t
	{
		ok = 0,
		not_found = 1,
		capacity_full = 2,
		internal_error = 3,
	};

#pragma pack(push, 1)

	struct ZoneServerHello
	{
		std::uint32_t server_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t world_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t map_instance_capacity = 0;
		std::uint16_t active_map_instance_count = 0;
		std::uint16_t active_player_count = 0;
		std::uint16_t load_score = 0;
		std::uint32_t flags = 0;
		char server_name[dc::k_service_name_max_len + 1]{};
	};

	struct ZoneServerRegisterAck
	{
		std::uint8_t accepted = 0;
		std::uint32_t server_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t world_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t map_instance_capacity = 0;
		std::uint16_t active_map_instance_count = 0;
		std::uint16_t active_player_count = 0;
		std::uint16_t load_score = 0;
		std::uint32_t flags = 0;
		char server_name[dc::k_service_name_max_len + 1]{};
	};

	struct ZoneServerRouteHeartbeat
	{
		std::uint32_t server_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t world_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t map_instance_capacity = 0;
		std::uint16_t active_map_instance_count = 0;
		std::uint16_t active_player_count = 0;
		std::uint16_t load_score = 0;
		std::uint32_t flags = 0;
	};

	struct WorldZonePlayerEnter
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t reserved = 0;
	};

	struct WorldZonePlayerLeave
	{
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t reserved = 0;
	};

	struct ZoneWorldPlayerEnterAck
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint16_t result_code = 0;
		std::uint16_t zone_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
	};

    struct WorldZoneMapAssignRequest
    {
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint32_t map_template_id = 0;
        std::uint32_t instance_id = 0;
        std::uint8_t create_if_missing = 0;
        std::uint8_t dungeon_instance = 0;
        std::uint16_t reserved = 0;
    };

	struct ZoneWorldMapAssignResponse
	{
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint16_t result_code = 0;
        std::uint16_t zone_id = 0;
        std::uint32_t map_template_id = 0;
        std::uint32_t instance_id = 0;
    };

	struct WorldZonePlayerMoveInternal
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::int32_t x = 0;
		std::int32_t y = 0;
	};

	struct ZoneWorldAoiSnapshot
	{
		std::uint64_t trace_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::int32_t self_x = 0;
		std::int32_t self_y = 0;
		std::uint16_t count = 0;
		std::uint16_t reserved = 0;
		proto::S2C_player_spawn_item items[1]{};
	};

	struct ZoneWorldAoiSpawnBatch
	{
		std::uint64_t trace_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t count = 0;
		std::uint16_t reserved = 0;
		proto::S2C_player_spawn_item items[1]{};
	};

	struct ZoneWorldAoiDespawnBatch
	{
		std::uint64_t trace_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t count = 0;
		std::uint16_t reserved = 0;
		proto::S2C_player_despawn_item items[1]{};
	};

	struct ZoneWorldAoiMoveBatch
	{
		std::uint64_t trace_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t char_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint16_t zone_id = 0;
		std::uint16_t channel_id = 0;
		std::uint16_t count = 0;
		std::uint16_t reserved = 0;
		proto::S2C_player_move_item items[1]{};
	};
#pragma pack(pop)

	inline constexpr std::size_t AoiSnapshotBodySize(std::size_t count) noexcept
	{
		return sizeof(ZoneWorldAoiSnapshot) - sizeof(proto::S2C_player_spawn_item) +
			count * sizeof(proto::S2C_player_spawn_item);
	}

	inline constexpr std::size_t AoiSpawnBatchBodySize(std::size_t count) noexcept
	{
		return sizeof(ZoneWorldAoiSpawnBatch) - sizeof(proto::S2C_player_spawn_item) +
			count * sizeof(proto::S2C_player_spawn_item);
	}

	inline constexpr std::size_t AoiDespawnBatchBodySize(std::size_t count) noexcept
	{
		return sizeof(ZoneWorldAoiDespawnBatch) - sizeof(proto::S2C_player_despawn_item) +
			count * sizeof(proto::S2C_player_despawn_item);
	}

	inline constexpr std::size_t AoiMoveBatchBodySize(std::size_t count) noexcept
	{
		return sizeof(ZoneWorldAoiMoveBatch) - sizeof(proto::S2C_player_move_item) +
			count * sizeof(proto::S2C_player_move_item);
	}

} // namespace proto::internal::world_zone
