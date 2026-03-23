#pragma once

#include <algorithm>
#include <cstdint>

#include "server_common/config/aoi_config.h"

namespace dc::cfg {

	inline std::uint32_t ClampU32Min(std::uint32_t v, std::uint32_t min_v, std::uint32_t fallback) noexcept
	{
		if (v < min_v) return fallback;
		return v;
	}

	inline int ClampIntMin(int v, int min_v, int fallback) noexcept
	{
		if (v < min_v) return fallback;
		return v;
	}

	inline void NormalizeShardAndRedisWait(
		std::uint32_t& db_shard_count,
		std::uint32_t& redis_shard_count,
		int& redis_wait_replicas,
		int& redis_wait_timeout_ms) noexcept
	{
		db_shard_count = ClampU32Min(db_shard_count, 1u, 1u);
		if (redis_shard_count == 0) {
			redis_shard_count = db_shard_count;
		}
		redis_shard_count = ClampU32Min(redis_shard_count, 1u, 1u);

		if (!(redis_wait_replicas > 0 && redis_wait_timeout_ms > 0)) {
			redis_wait_replicas = 0;
			redis_wait_timeout_ms = 0;
		}
	}

	inline void NormalizeAoiConfig(AoiConfig& cfg) noexcept
	{
		cfg.map_size.x = std::max(1, cfg.map_size.x);
		cfg.map_size.y = std::max(1, cfg.map_size.y);
		cfg.world_sight_unit = std::max(1, cfg.world_sight_unit);
		cfg.aoi_radius_cells = std::max(0, cfg.aoi_radius_cells);
	}

} // namespace dc::cfg

