#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <memory>
#include <deque>
#include <cstring>
#include <chrono>

#include "net/actor/actor_system.h"
#include "proto/common/proto_base.h"
#include "proto/common/packet_util.h"

#include "services/world/bench/bench_stats.h"

namespace svr {

	// ---- ActorId tagging ----
	//  - PlayerActor : actor_id = char_id (assume char_id < 2^63)
	//  - WorldActor  : actor_id = 0
	//  - ZoneActor   : actor_id = (1<<63) | zone_id
	constexpr std::uint64_t kZoneTag = 1ull << 63;
	inline constexpr std::uint64_t MakeZoneActorId(std::uint32_t zone_id) noexcept { return kZoneTag | (std::uint64_t)zone_id; }
	inline constexpr bool IsZoneActorId(std::uint64_t actor_id) noexcept { return (actor_id & kZoneTag) != 0; }
	inline constexpr std::uint32_t ZoneIdFromActorId(std::uint64_t actor_id) noexcept { return (std::uint32_t)(actor_id & ~kZoneTag); }

} // namespace svr
