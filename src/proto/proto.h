#pragma once
#include "types.h"

namespace proto {

	constexpr int max_world_name_len = 32;

	enum C2SMsg : u16 {
		open_world_notice = 1,
		add_gold = 2,
		actor_seq_test = 100,
		actor_forward = 101,
	};

	enum S2CMsg : u16 {
		open_world_success = 1,
		add_gold_ok = 2,
		actor_bound = 100,
		actor_seq_ack = 101,
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
#pragma pack(pop)

	static_assert(sizeof(C2S_open_world_notice) == (max_world_name_len + 1) * sizeof(char));
	static_assert(sizeof(S2C_open_world_success) == 4);
	static_assert(sizeof(C2S_add_gold) == 4);
	static_assert(sizeof(S2C_add_gold_ok) == 8);
	static_assert(sizeof(C2S_actor_seq_test) == 8);
	static_assert(sizeof(C2S_actor_forward) == 16);
	static_assert(sizeof(S2C_actor_bound) == 8);
	static_assert(sizeof(S2C_actor_seq_ack) == 16);

} // namespace proto
