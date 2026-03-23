#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "proto/common/proto_base.h"

namespace svr::aoi {

	inline constexpr std::uint16_t kMaxBatchEntityCount = 4096;

	template <typename Range>
	std::vector<std::uint64_t> SanitizeEntityIds(const Range& ids)
	{
		std::vector<std::uint64_t> out;
		out.reserve(ids.size());

		std::unordered_set<std::uint64_t> seen;
		seen.reserve(ids.size());

		for (auto id : ids) {
			if (id == 0) {
				continue;
			}
			if (!seen.insert(id).second) {
				continue;
			}
			out.push_back(id);
		}
		return out;
	}

	inline std::uint16_t ClampBatchEntityCount(std::size_t count) noexcept
	{
		return static_cast<std::uint16_t>(std::min<std::size_t>(count, kMaxBatchEntityCount));
	}

	inline std::size_t SpawnBatchBodySize(std::uint16_t count) noexcept
	{
		if (count == 0) {
			return 0;
		}
		return sizeof(proto::S2C_player_spawn_batch)
			+ (static_cast<std::size_t>(count) - 1) * sizeof(proto::S2C_player_spawn_item);
	}

	inline std::size_t DespawnBatchBodySize(std::uint16_t count) noexcept
	{
		if (count == 0) {
			return 0;
		}
		return sizeof(proto::S2C_player_despawn_batch)
			+ (static_cast<std::size_t>(count) - 1) * sizeof(proto::S2C_player_despawn_item);
	}

} // namespace svr::aoi

