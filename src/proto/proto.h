#pragma once
#include "types.h"

namespace proto {

	constexpr int max_world_name_len = 32;

	enum C2SMsg : u16 {
		open_world_notice = 1,
		add_gold = 2,

		// ---- Basic combat test ----
		get_stats = 10,
		heal_self = 11,
		spawn_monster = 20,
		attack_monster = 21,
		attack_player = 30,

		move = 40,

		// internal/test: actor ordering


		actor_seq_test = 100,
		actor_forward = 101,
		bench_move = 102,
	};

	enum S2CMsg : u16 {
		open_world_success = 1,
		add_gold_ok = 2,

		// ---- Basic combat test ----
		stats = 10,
		spawn_monster_ok = 20,
		attack_result = 21,

		player_spawn = 40,
		player_despawn = 41,
		player_move = 42,


		actor_bound = 100,
		actor_seq_ack = 101,
		bench_move_ack = 102,
	};

#pragma pack(push, 1)
	struct C2S_open_world_notice {
		char szWorldName[max_world_name_len + 1];
		C2S_open_world_notice() { zero(*this); }
	};

	struct S2C_open_world_success {
		u32 ok; // 1=ok
		S2C_open_world_success() : ok(1) {}
	};

	// ---- Demo: gold 증가 요청/응답 ----
	struct C2S_add_gold {
		u32 add; // 증가량
		C2S_add_gold() : add(0) {}
	};

	struct S2C_add_gold_ok {
		u32 ok;    // 1=ok
		u32 gold;  // 적용 후 골드
		S2C_add_gold_ok() : ok(1), gold(0) {}
	};

	// ---- Basic combat test: stats/monster/combat ----
	struct C2S_get_stats {
		u32 reserved;
		C2S_get_stats() : reserved(0) {}
	};

	struct S2C_stats {
		u64 char_id;
		u32 hp;
		u32 max_hp;
		u32 atk;
		u32 def;
		u32 gold;
		S2C_stats() : char_id(0), hp(0), max_hp(0), atk(0), def(0), gold(0) {}
	};

	struct C2S_heal_self {
		u32 amount; // 회복량(0이면 full)
		C2S_heal_self() : amount(0) {}
	};

	struct C2S_spawn_monster {
		u32 template_id; // 0이면 기본
		C2S_spawn_monster() : template_id(0) {}
	};

	struct S2C_spawn_monster_ok {
		u64 monster_id;
		u32 hp;
		u32 atk;
		u32 def;
		S2C_spawn_monster_ok() : monster_id(0), hp(0), atk(0), def(0) {}
	};

	struct C2S_attack_monster {
		u64 monster_id;
		C2S_attack_monster() : monster_id(0) {}
	};

	struct C2S_attack_player {
		u64 target_char_id; // 맞을 캐릭터(char_id)
		C2S_attack_player() : target_char_id(0) {}
	};

	// ---- Zone/AOI test ----
	struct C2S_move {
		i32 x;
		i32 y;
		C2S_move() : x(0), y(0) {}
	};

	struct S2C_player_spawn {
		u64 char_id;
		i32 x;
		i32 y;
		S2C_player_spawn() : char_id(0), x(0), y(0) {}
	};

	struct S2C_player_despawn {
		u64 char_id;
		S2C_player_despawn() : char_id(0) {}
	};

	struct S2C_player_move {
		u64 char_id;
		i32 x;
		i32 y;
		S2C_player_move() : char_id(0), x(0), y(0) {}
	};

	// 공통 전투 결과(몬스터/플레이어)
	struct S2C_attack_result {
		u64 attacker_id;
		u64 target_id;     // monster_id 또는 char_id
		u32 damage;
		u32 target_hp;
		u32 killed;        // 1이면 사망/처치
		u32 drop_item_id;  // 처치 시 드랍(없으면 0)
		u32 drop_count;
		u32 attacker_gold; // 적용 후 골드(샘플)
		S2C_attack_result() : attacker_id(0), target_id(0), damage(0), target_hp(0), killed(0),
			drop_item_id(0), drop_count(0), attacker_gold(0) {}
	};

	// ---- Actor/멀티 로직 테스트 ----
	struct C2S_actor_seq_test {
		u32 seq;      // 단일 연결에서 증가시키는 seq
		u32 work_us;  // 서버에서 작업(바쁜일/슬립) 시뮬레이션
		C2S_actor_seq_test() : seq(0), work_us(0) {}
	};

	// 여러 세션이 "같은 Actor"로 몰아넣는 테스트(ResolveActorIdForPacket 사용)
	struct C2S_actor_forward {
		u64 target_actor_id; // 보낼 대상 ActorId (char_id)
		u32 work_us;
		u32 tag;             // 디버깅용
		C2S_actor_forward() : target_actor_id(0), work_us(0), tag(0) {}
	};

	struct S2C_actor_bound {
		u64 actor_id; // 서버가 바인딩한 char_id
		S2C_actor_bound() : actor_id(0) {}
	};

	struct S2C_actor_seq_ack {
		u32 ok;      // 1=ok
		u32 seq;
		u32 shard;   // 서버 Actor shard index(디버그)
		u32 errors;  // 서버 측 감지 에러 누적(디버그)
		S2C_actor_seq_ack() : ok(1), seq(0), shard(0), errors(0) {}
	};

	// ---- Load test: move + RTT 측정 ----
	// - client_ts_ns: 클라 monotonic time(ns)를 그대로 echo해서 RTT 계산
	struct C2S_bench_move {
		u32 seq;
		u32 work_us;        // 서버에서 추가 작업 시뮬레이션(0이면 없음)
		i32 x;
		i32 y;
		u64 client_ts_ns;
		C2S_bench_move() : seq(0), work_us(0), x(0), y(0), client_ts_ns(0) {}
	};

	struct S2C_bench_move_ack {
		u32 ok;
		u32 seq;
		u64 client_ts_ns;
		u32 zone;   // 디버그
		u32 shard;  // 디버그
		S2C_bench_move_ack() : ok(1), seq(0), client_ts_ns(0), zone(0), shard(0) {}
	};
#pragma pack(pop)

	static_assert(sizeof(C2S_open_world_notice) == (max_world_name_len + 1) * sizeof(char));
	static_assert(sizeof(S2C_open_world_success) == 4);
	static_assert(sizeof(C2S_add_gold) == 4);
	static_assert(sizeof(S2C_add_gold_ok) == 8);
	static_assert(sizeof(C2S_get_stats) == 4);
	static_assert(sizeof(S2C_stats) == 28);
	static_assert(sizeof(C2S_heal_self) == 4);
	static_assert(sizeof(C2S_spawn_monster) == 4);
	static_assert(sizeof(S2C_spawn_monster_ok) == 20);
	static_assert(sizeof(C2S_attack_monster) == 8);
	static_assert(sizeof(C2S_attack_player) == 8);
	static_assert(sizeof(C2S_move) == 8);
	static_assert(sizeof(S2C_player_spawn) == 16);
	static_assert(sizeof(S2C_player_despawn) == 8);
	static_assert(sizeof(S2C_player_move) == 16);
	static_assert(sizeof(S2C_attack_result) == 40);
	static_assert(sizeof(C2S_actor_seq_test) == 8);
	static_assert(sizeof(C2S_actor_forward) == 16);
	static_assert(sizeof(S2C_actor_bound) == 8);
	static_assert(sizeof(S2C_actor_seq_ack) == 16);
	static_assert(sizeof(C2S_bench_move) == 24);
	static_assert(sizeof(S2C_bench_move_ack) == 24);

} // namespace proto
