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
#include "services/world/actors/world_actor_ids.h"

namespace svr {
	struct CharCombatState {
		std::uint32_t hp = 100;
		std::uint32_t max_hp = 100;
		std::uint32_t atk = 20;
		std::uint32_t def = 3;
		std::uint32_t gold = 1000;
	};

	// ---- 샘플 몬스터 ----
	struct MonsterState {
		std::uint64_t id = 0;
		std::uint32_t hp = 50;
		std::uint32_t atk = 8;
		std::uint32_t def = 1;
		std::uint32_t drop_item_id = 1001;
		std::uint32_t drop_count = 1;
	};

	struct Vec2i {
		std::int32_t x = 0;
		std::int32_t y = 0;
	};

	// ✅ WorldActor(ActorId=0): 월드 공유 상태(몬스터 등)

} // namespace svr
