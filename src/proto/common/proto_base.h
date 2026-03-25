#pragma once
#include "types.h"
#include "shared/constants.h"

namespace proto {

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
		bench_reset = 103,
		bench_measure = 104,
	};

	enum S2CMsg : u16 {
		open_world_success = 1,
		add_gold_ok = 2,

		// ---- Basic combat test ----
		stats = 10,
		zone_map_state = 12,
		spawn_monster_ok = 20,
		attack_result = 21,

		player_spawn = 40,
		player_despawn = 41,
		player_move = 42,
		player_move_batch = 43,
		player_spawn_batch = 44,
		player_despawn_batch = 45,

		actor_bound = 100,
		actor_seq_ack = 101,
		bench_move_ack = 102,
	};

#pragma pack(push, 1)
	struct C2S_open_world_notice {
		char szWorldName[dc::k_max_world_name_len + 1];
		C2S_open_world_notice() { zero(*this); }
	};

	struct S2C_open_world_success {
		u32 ok;
		S2C_open_world_success() : ok(1) {}
	};

	struct C2S_add_gold {
		u32 add;
		C2S_add_gold() : add(0) {}
	};

	struct S2C_add_gold_ok {
		u32 ok;
		u32 gold;
		S2C_add_gold_ok() : ok(1), gold(0) {}
	};

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

	enum class ZoneMapStateReason : u16 {
		enter_success = 0,
		position_update = 1,
		zone_changed = 2,
		portal_moved = 3,
	};

	struct S2C_zone_map_state {
		u64 char_id;
		u32 zone_id;
		u32 map_id;
		i32 x;
		i32 y;
		u16 reason;
		S2C_zone_map_state() : char_id(0), zone_id(0), map_id(0), x(0), y(0), reason((u16)ZoneMapStateReason::enter_success) {}
	};

	struct C2S_heal_self {
		u32 amount;
		C2S_heal_self() : amount(0) {}
	};

	struct C2S_spawn_monster {
		u32 template_id;
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
		u64 target_char_id;
		C2S_attack_player() : target_char_id(0) {}
	};

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

	struct S2C_player_spawn_item {
		u64 char_id;
		i32 x;
		i32 y;
		S2C_player_spawn_item() : char_id(0), x(0), y(0) {}
	};

	struct S2C_player_spawn_batch {
		u16 count;
		S2C_player_spawn_item items[1];
		S2C_player_spawn_batch() : count(0) {}
	};

	struct S2C_player_despawn_item {
		u64 char_id;
		S2C_player_despawn_item() : char_id(0) {}
	};

	struct S2C_player_despawn_batch {
		u16 count;
		S2C_player_despawn_item items[1];
		S2C_player_despawn_batch() : count(0) {}
	};

	struct S2C_player_move {
		u64 char_id;
		i32 x;
		i32 y;
		S2C_player_move() : char_id(0), x(0), y(0) {}
	};

	struct S2C_player_move_item {
		u64 char_id;
		i32 x;
		i32 y;
		S2C_player_move_item() : char_id(0), x(0), y(0) {}
	};

	struct S2C_player_move_batch {
		u16 count;
		S2C_player_move_item items[1];
		S2C_player_move_batch() : count(0) {}
 	};

	struct S2C_attack_result {
		u64 attacker_id;
		u64 target_id;
		u32 damage;
		u32 target_hp;
		u32 killed;
		u32 drop_item_id;
		u32 drop_count;
		u32 attacker_gold;
		S2C_attack_result() : attacker_id(0), target_id(0), damage(0), target_hp(0), killed(0),
			drop_item_id(0), drop_count(0), attacker_gold(0) {}
	};

	struct C2S_actor_seq_test {
		u32 seq;
		u32 work_us;
		C2S_actor_seq_test() : seq(0), work_us(0) {}
	};

	struct C2S_actor_forward {
		u64 target_actor_id;
		u32 work_us;
		u32 tag;
		C2S_actor_forward() : target_actor_id(0), work_us(0), tag(0) {}
	};

	struct S2C_actor_bound {
		u64 actor_id;
		S2C_actor_bound() : actor_id(0) {}
	};

	struct S2C_actor_seq_ack {
		u32 ok;
		u32 seq;
		u32 shard;
		u32 errors;
		S2C_actor_seq_ack() : ok(1), seq(0), shard(0), errors(0) {}
	};

	struct C2S_bench_move {
		u32 seq;
		u32 work_us;
		i32 x;
		i32 y;
		u64 client_ts_ns;
		C2S_bench_move() : seq(0), work_us(0), x(0), y(0), client_ts_ns(0) {}
	};

	struct C2S_bench_reset {
		u32 reserved;
		C2S_bench_reset() : reserved(0) {}
	};

	struct C2S_bench_measure {
		u32 seconds;
		C2S_bench_measure() : seconds(0) {}
	};

	struct S2C_bench_move_ack {
		u32 ok;
		u32 seq;
		u64 client_ts_ns;
		u32 zone;
		u32 shard;
		S2C_bench_move_ack() : ok(1), seq(0), client_ts_ns(0), zone(0), shard(0) {}
	};
#pragma pack(pop)

} // namespace proto
