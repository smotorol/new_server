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
class PlayerActor final : public net::IActor {
	public:
		explicit PlayerActor(std::uint64_t char_id_) : char_id(char_id_) {}

		void bind_session(std::uint32_t sid_, std::uint32_t serial_) {
			sid = sid_;
			serial = serial_;
			online = true;
		}
		void unbind_session(std::uint32_t sid_, std::uint32_t serial_) {
			// stale disconnect 방어: sid/serial이 다르면 무시
			if (sid != sid_ || serial != serial_) return;
			online = false;
			sid = kInvalidSid;
			serial = 0;
		}
		bool has_session() const { return online && sid != kInvalidSid; }

		bool CanAddItem(std::uint32_t item_id, std::uint32_t count) const {
			(void)count;
			// 샘플: 서로 다른 아이템 종류 최대 30개
			if (item_id == 0) return true;
			if (items.find(item_id) != items.end()) return true;
			return items.size() < 30;
		}

		void CommitLoot(std::uint64_t tx_id, std::uint32_t item_id, std::uint32_t count, std::uint32_t add_gold) {
			// 멱등 커밋
			if (tx_id != 0) {
				if (committed_loot_txs.find(tx_id) != committed_loot_txs.end()) return;
				committed_loot_txs.insert(tx_id);
				// 너무 커지면 적당히 trim (샘플)
				if (committed_loot_txs.size() > 2048) {
					committed_loot_txs.clear();
				}
			}
			if (item_id != 0 && count != 0) items[item_id] += count;
			combat.gold += add_gold;
		}

	public:
		std::uint64_t char_id = 0;
		bool online = false;
		static constexpr std::uint32_t kInvalidSid = 0xFFFFFFFFu;
		std::uint32_t sid = kInvalidSid;
		std::uint32_t serial = 0;

		// zone/aoi
		std::uint32_t zone_id = 1;
		std::uint32_t map_template_id = 1001;
		std::uint32_t map_instance_id = 0;
		Vec2i pos{};

		CharCombatState combat{};
		std::unordered_map<std::uint32_t, std::uint32_t> items; // item_id -> count
		std::unordered_set<std::uint64_t> committed_loot_txs;    // 멱등 보장
	};
} // namespace svr
