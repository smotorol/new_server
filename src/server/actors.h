#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../net/actor_system.h"

namespace svr {

	// ---- ActorId tagging ----
	//  - PlayerActor : actor_id = char_id (assume char_id < 2^63)
	//  - WorldActor  : actor_id = 0
	//  - ZoneActor   : actor_id = (1<<63) | zone_id
	constexpr std::uint64_t kZoneTag = 1ull << 63;
	inline constexpr std::uint64_t MakeZoneActorId(std::uint32_t zone_id) noexcept { return kZoneTag | (std::uint64_t)zone_id; }
	inline constexpr bool IsZoneActorId(std::uint64_t actor_id) noexcept { return (actor_id & kZoneTag) != 0; }
	inline constexpr std::uint32_t ZoneIdFromActorId(std::uint64_t actor_id) noexcept { return (std::uint32_t)(actor_id & ~kZoneTag); }

	// ---- 기본 전투 상태(샘플) ----
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
	class WorldActor final : public net::IActor {
	public:
		// 필요 시: global chat / world event 등
	};

	// ✅ PlayerActor(ActorId=char_id): 캐릭터 상태를 독점
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
		Vec2i pos{};

		CharCombatState combat{};
		std::unordered_map<std::uint32_t, std::uint32_t> items; // item_id -> count
		std::unordered_set<std::uint64_t> committed_loot_txs;    // 멱등 보장
	};

	// ✅ ZoneActor: AOI/브로드캐스트 + 몬스터/드랍 coordinator
	class ZoneActor final : public net::IActor {
	public:
		static constexpr std::int32_t kCellSize = 10; // 샘플: 10 단위

		struct PlayerInfo {
			Vec2i pos{};
			std::int32_t cx = 0;
			std::int32_t cy = 0;
		};

		std::uint64_t next_monster_id = 1;
		std::uint64_t next_tx_id = 1;
		std::unordered_map<std::uint64_t, MonsterState> monsters;

		std::unordered_map<std::uint64_t, PlayerInfo> players; // char_id -> info
		std::unordered_map<std::int64_t, std::unordered_set<std::uint64_t>> cells; // cell_key -> set<char_id>

		static std::int32_t ToCell(std::int32_t v) {
			// floor division for negatives
			if (v >= 0) return v / kCellSize;
			return -(((-v) + kCellSize - 1) / kCellSize);
		}
		static std::int64_t CellKey(std::int32_t cx, std::int32_t cy) {
			return (std::int64_t(cx) << 32) ^ (std::uint32_t)cy;
		}

		std::unordered_set<std::uint64_t> GatherNeighbors(std::int32_t cx, std::int32_t cy) {
			std::unordered_set<std::uint64_t> out;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					auto it = cells.find(CellKey(cx + dx, cy + dy));
					if (it == cells.end()) continue;
					for (auto id : it->second) out.insert(id);
				}
			}
			return out;
		}

		void Join(std::uint64_t char_id, Vec2i pos) {
			auto& pi = players[char_id];
			pi.pos = pos;
			pi.cx = ToCell(pos.x);
			pi.cy = ToCell(pos.y);
			cells[CellKey(pi.cx, pi.cy)].insert(char_id);
		}

		void Leave(std::uint64_t char_id) {
			auto it = players.find(char_id);
			if (it == players.end()) return;
			auto ck = CellKey(it->second.cx, it->second.cy);
			auto itc = cells.find(ck);
			if (itc != cells.end()) {
				itc->second.erase(char_id);
				if (itc->second.empty()) cells.erase(itc);
			}
			players.erase(it);
		}

		// Move는 ZoneActor 밖에서 old/new visible 계산 및 브로드캐스트용으로 사용하기 위해
		// old/new visible set을 반환한다.
		struct MoveDiff {
			Vec2i new_pos{};
			std::unordered_set<std::uint64_t> old_vis;
			std::unordered_set<std::uint64_t> new_vis;
		};

		MoveDiff Move(std::uint64_t char_id, Vec2i new_pos) {
			MoveDiff d{};
			d.new_pos = new_pos;
			auto it = players.find(char_id);
			if (it == players.end()) {
				Join(char_id, new_pos);
				// old_vis empty, new_vis will be based on current
				auto& pi = players[char_id];
				d.new_vis = GatherNeighbors(pi.cx, pi.cy);
				d.new_vis.erase(char_id);
				return d;
			}

			auto& pi = it->second;
			d.old_vis = GatherNeighbors(pi.cx, pi.cy);
			d.old_vis.erase(char_id);

			const std::int32_t ncx = ToCell(new_pos.x);
			const std::int32_t ncy = ToCell(new_pos.y);

			if (ncx != pi.cx || ncy != pi.cy) {
				// remove old cell
				auto oldk = CellKey(pi.cx, pi.cy);
				auto itc = cells.find(oldk);
				if (itc != cells.end()) {
					itc->second.erase(char_id);
					if (itc->second.empty()) cells.erase(itc);
				}
				// add new cell
				cells[CellKey(ncx, ncy)].insert(char_id);
				pi.cx = ncx;
				pi.cy = ncy;
			}
			pi.pos = new_pos;

			d.new_vis = GatherNeighbors(pi.cx, pi.cy);
			d.new_vis.erase(char_id);
			return d;
		}
	};

} // namespace svr
