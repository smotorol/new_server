#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <memory>
#include <optional>
#include <deque>
#include <cstring>
#include <chrono>

#include "net/actor/actor_system.h"
#include "proto/common/proto_base.h"
#include "proto/common/packet_util.h"

#include "services/world/bench/bench_stats.h"
#include "services/world/common/character_combat_stat_calculator.h"
#include "services/world/common/character_core_state.h"
#include "services/world/common/demo_char_state.h"

namespace svr {
class PlayerActor final : public net::IActor {
	public:
		explicit PlayerActor(std::uint64_t char_id_) : char_id(char_id_) {}

		CharacterCoreState& MutableCoreState() {
			if (!core_state.has_value()) {
				core_state = MakeDefaultCharacterCoreState(0, char_id);
				core_state->identity.char_id = char_id;
			}
			return *core_state;
		}

		const CharacterCoreState* CoreState() const noexcept { return core_state ? &(*core_state) : nullptr; }
		const CharacterRuntimeHotState* HotState() const noexcept { return core_state ? &core_state->hot : nullptr; }
		const CharacterCombatRuntimeStats* CombatStats() const noexcept { return core_state ? &core_state->combat_stats : nullptr; }
		CharacterRuntimeHotState& MutableHotState() { return MutableCoreState().hot; }

		Vec2i GetPosition() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return { hot->position.x, hot->position.y };
			}
			return pos;
		}

		std::uint32_t GetZoneId() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->position.zone_id;
			}
			return zone_id;
		}

		std::uint32_t GetMapId() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->position.map_id;
			}
			return map_template_id;
		}

		std::uint32_t GetMapInstanceId() const noexcept { return map_instance_id; }

		std::uint32_t GetCurrentHp() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->resources.current_hp;
			}
			return combat.hp;
		}

		std::uint32_t GetCurrentMp() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->resources.current_mp;
			}
			return 0;
		}

		std::uint32_t GetMaxHp() const noexcept {
			if (const auto* stats = CombatStats(); stats != nullptr) {
				return stats->max_hp;
			}
			return combat.max_hp;
		}

		std::uint32_t GetMaxMp() const noexcept {
			if (const auto* stats = CombatStats(); stats != nullptr) {
				return stats->max_mp;
			}
			return 0;
		}

		std::uint32_t GetAttack() const noexcept {
			if (const auto* stats = CombatStats(); stats != nullptr) {
				return stats->atk;
			}
			return combat.atk;
		}

		std::uint32_t GetDefense() const noexcept {
			if (const auto* stats = CombatStats(); stats != nullptr) {
				return stats->def;
			}
			return combat.def;
		}

		std::uint32_t GetGold() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->resources.gold;
			}
			return combat.gold;
		}

		bool IsAlive() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->alive;
			}
			return combat.hp > 0;
		}

		bool IsInWorld() const noexcept {
			if (const auto* hot = HotState(); hot != nullptr) {
				return hot->in_world;
			}
			return online;
		}

		void SyncCombatProjectionFromAuthoritative() {
			if (!core_state.has_value()) {
				return;
			}

			const auto& stats = core_state->combat_stats;
			auto& hot = core_state->hot;
			hot.resources.max_hp = stats.max_hp;
			hot.resources.max_mp = stats.max_mp;
			hot.resources.current_hp = std::min(hot.resources.current_hp, stats.max_hp);
			hot.resources.current_mp = std::min(hot.resources.current_mp, stats.max_mp);
			combat.max_hp = stats.max_hp;
			combat.atk = stats.atk;
			combat.def = stats.def;
		}

		void ApplyCombatRuntimeStats(const CharacterCombatRuntimeStats& stats, bool clamp_current = true) {
			auto& core = MutableCoreState();
			core.combat_stats = stats;
			if (clamp_current) {
				core.hot.resources.current_hp = std::min(core.hot.resources.current_hp, stats.max_hp);
				core.hot.resources.current_mp = std::min(core.hot.resources.current_mp, stats.max_mp);
				core.hot.alive = (core.hot.resources.current_hp > 0);
			}
			SyncCombatProjectionFromAuthoritative();
			SyncProjectionFromCoreState();
		}

		void RecomputeCombatRuntimeStats() {
			ApplyCombatRuntimeStats(RecomputeCombatStats(MutableCoreState()));
		}

		// Reuse the same recompute path from future equipment/job change handlers.
		void OnEquipmentSummaryChanged(const EquipSummary& equip) {
			MutableCoreState().equip = equip;
			RecomputeCombatRuntimeStats();
		}

		void OnCombatIdentityChanged(std::uint16_t job, std::uint16_t tribe) {
			auto& identity = MutableCoreState().identity;
			identity.job = job;
			identity.tribe = tribe;
			RecomputeCombatRuntimeStats();
		}

		void SyncProjectionFromCoreState() {
			if (!core_state.has_value()) {
				return;
			}

			const auto& hot = core_state->hot;
			zone_id = hot.position.zone_id;
			map_template_id = hot.position.map_id;
			pos = { hot.position.x, hot.position.y };
			online = hot.in_world && sid != kInvalidSid;
			combat.hp = hot.resources.current_hp;
			combat.gold = hot.resources.gold;
			SyncCombatProjectionFromAuthoritative();
		}

		void MarkHotDirty(CharacterDirtyFlags flags, bool bump_version = true) {
			auto& hot = MutableHotState();
			hot.dirty_flags |= flags;
			if (bump_version) {
				++hot.version;
			}
		}

		void SetInWorld(bool in_world) {
			auto& hot = MutableHotState();
			hot.in_world = in_world;
			MarkHotDirty(CharacterDirtyFlags::position);
			SyncProjectionFromCoreState();
		}

		void SetAlive(bool alive) {
			auto& hot = MutableHotState();
			hot.alive = alive;
			if (!alive) {
				hot.resources.current_hp = 0;
			}
			MarkHotDirty(CharacterDirtyFlags::resources);
			SyncProjectionFromCoreState();
		}

		void SetCurrentHp(std::uint32_t hp) {
			auto& hot = MutableHotState();
			hot.resources.current_hp = std::min(hp, GetMaxHp());
			hot.alive = (hot.resources.current_hp > 0);
			MarkHotDirty(CharacterDirtyFlags::resources);
			SyncProjectionFromCoreState();
		}

		void SetCurrentMp(std::uint32_t mp) {
			auto& hot = MutableHotState();
			hot.resources.current_mp = std::min(mp, GetMaxMp());
			MarkHotDirty(CharacterDirtyFlags::resources);
		}

		void AddGold(std::uint32_t add_gold) {
			auto& hot = MutableHotState();
			hot.resources.gold += add_gold;
			MarkHotDirty(CharacterDirtyFlags::resources);
			SyncProjectionFromCoreState();
		}

		void SetWorldPosition(
			std::uint32_t zone_id_,
			std::uint32_t map_id_,
			std::uint32_t map_instance_id_,
			std::int32_t x,
			std::int32_t y,
			std::int32_t z = 0,
			std::int16_t heading = 0)
		{
			auto& hot = MutableHotState();
			hot.position.zone_id = zone_id_;
			hot.position.map_id = map_id_;
			hot.position.x = x;
			hot.position.y = y;
			hot.position.z = z;
			hot.position.heading = heading;
			map_instance_id = map_instance_id_;
			MarkHotDirty(CharacterDirtyFlags::position);
			SyncProjectionFromCoreState();
		}

		void ClearWorldPosition() {
			auto& hot = MutableHotState();
			hot.position.zone_id = 0;
			hot.position.map_id = 0;
			hot.position.x = 0;
			hot.position.y = 0;
			hot.position.z = 0;
			hot.position.heading = 0;
			map_instance_id = 0;
			hot.in_world = false;
			MarkHotDirty(CharacterDirtyFlags::position);
			SyncProjectionFromCoreState();
		}

		std::string SerializePersistentState() const {
			if (core_state.has_value()) {
				return demo::SerializeDemo(*core_state);
			}

			demo::DemoCharState fallback{};
			fallback.char_id = char_id;
			fallback.gold = combat.gold;
			return demo::SerializeDemo(fallback);
		}

		void bind_session(std::uint32_t sid_, std::uint32_t serial_) {
			sid = sid_;
			serial = serial_;
			online = true;
			if (core_state.has_value()) {
				core_state->hot.in_world = true;
				SyncProjectionFromCoreState();
			}
		}
		void unbind_session(std::uint32_t sid_, std::uint32_t serial_) {
			// stale disconnect 방어: sid/serial이 다르면 무시
			if (sid != sid_ || serial != serial_) return;
			online = false;
			sid = kInvalidSid;
			serial = 0;
			if (core_state.has_value()) {
				core_state->hot.in_world = false;
				MarkHotDirty(CharacterDirtyFlags::position);
			}
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
			AddGold(add_gold);
		}

	public:
		std::uint64_t char_id = 0;
		bool online = false;
		static constexpr std::uint32_t kInvalidSid = 0xFFFFFFFFu;
		std::uint32_t sid = kInvalidSid;
		std::uint32_t serial = 0;

		// Deprecated projection fields only; CharacterCoreState.hot is authoritative.
		// Keep gameplay reads/writes on accessor/helper paths and use SyncProjectionFromCoreState()
		// only for compatibility with existing code that still needs these mirrors.
		std::uint32_t zone_id = 1;
		std::uint32_t map_template_id = 1001;
		std::uint32_t map_instance_id = 0;
		Vec2i pos{};

		std::optional<CharacterCoreState> core_state;
		// Deprecated projection for network/gameplay compatibility.
		// Authoritative current values live in CharacterCoreState.hot and
		// authoritative combat stat caps/derived values live in CharacterCoreState.combat_stats.
		CharCombatState combat{};
		std::unordered_map<std::uint32_t, std::uint32_t> items; // item_id -> count
		std::unordered_set<std::uint64_t> committed_loot_txs;    // 멱등 보장
	};
} // namespace svr

