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
#include "services/world/actors/zone_aoi_types.h"

namespace svr {
class WorldActor final : public net::IActor {
	public:
		// 필요 시: global chat / world event 등
	};
} // namespace svr
