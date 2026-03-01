#pragma once

#include <cstdint>
#include <unordered_map>

#include "../net/actor_system.h"

namespace svr {

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

	// ✅ WorldActor(ActorId=0): 월드 공유 상태(몬스터 등)
	class WorldActor final : public net::IActor {
	public:
		std::uint64_t next_monster_id = 1;
		std::unordered_map<std::uint64_t, MonsterState> monsters;
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
			sid = 0;
			serial = 0;
		}

	public:
		std::uint64_t char_id = 0;
		bool online = false;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;

		CharCombatState combat{};
	};

} // namespace svr
