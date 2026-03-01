#include "networkex.h"
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <thread>
#include "../common/string_utils.h"
#include "../proto/packet_util.h"
#include "../proto/proto.h"
#include "../net/actor_system.h"
#include "mainthread.h"
#include "dqs_payloads.h"
#include "actors.h"


// ===== 샘플 캐릭터 상태(바이너리 blob) =====
#pragma pack(push, 1)
struct DemoCharState
{
	std::uint64_t char_id = 0;
	std::uint32_t gold = 0;
	std::uint32_t version = 0;
};
#pragma pack(pop)
static_assert(sizeof(DemoCharState) == 16);

static std::string SerializeDemo(const DemoCharState& s)
{
	return std::string(reinterpret_cast<const char*>(&s), sizeof(s));
}
static bool TryDeserializeDemo(const std::string& blob, DemoCharState& out)
{
	if (blob.size() != sizeof(DemoCharState)) return false;
	std::memcpy(&out, blob.data(), sizeof(DemoCharState));
	return true;
}

std::uint64_t CNetworkEX::GetActorIdBySession(std::uint32_t sid) const
{
	std::lock_guard lk(state_mtx_);
	auto it = session_char_ids_.find(sid);
	if (it != session_char_ids_.end() && it->second != 0)
		return it->second;
	return static_cast<std::uint64_t>(sid);
}

std::uint64_t CNetworkEX::ResolveActorId(std::uint32_t session_idx) const
{
	// World 라인은 (sid -> char_id) 바인딩 이후 char_id Actor로 라우팅
	return GetActorIdBySession(session_idx);
}

std::uint64_t CNetworkEX::ResolveActorIdForPacket(std::uint32_t session_idx,
	const _MSG_HEADER& header, const char* body, std::size_t body_len,
	std::uint64_t default_actor) const
{
	(void)session_idx;
	const std::uint16_t type = proto::get_type_u16(header);
	if (type == (std::uint16_t)proto::C2SMsg::actor_forward) {
		auto* req = proto::as<proto::C2S_actor_forward>(body, body_len);
		if (req && req->target_actor_id != 0) return req->target_actor_id;
	}

	// ---- Basic combat test routing ----
	// 몬스터 생성은 "월드 Actor(0)"에서 처리
	if (type == (std::uint16_t)proto::C2SMsg::spawn_monster) {
		return 0; // world actor
	}
	// 몬스터 공격은 "공격자 Actor"에서 받아서, 내부에서 월드 Actor로 메시지를 넘기는 방식으로 처리
	// (공격자 스탯을 안전하게 읽고, 월드 공유 상태는 월드 Actor에서만 수정)

	// PvP는 "맞는 쪽(target_char_id)" Actor로 라우팅해서 타겟 HP를 단일 실행으로 갱신
	if (type == (std::uint16_t)proto::C2SMsg::attack_player) {
		auto* req = proto::as<proto::C2S_attack_player>(body, body_len);
		if (req && req->target_char_id != 0) return req->target_char_id;
	}

	return default_actor;
}
bool CNetworkEX::DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	if (!pMsgHeader) return false;

	switch (dwProID)
	{
	case eLine_World:
		{
			return WorldLineAnalysis(dwProID, dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	case eLine_Login:
		{
			return LoginLineAnalysis(dwProID, dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	case eLine_Control:
		{
			return ControlLineAnalysis(dwProID, dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	}
	return false;
}

void CNetworkEX::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("CNetworkEX::AcceptClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);

	// ===== 샘플 플로우는 World 라인에서만 시연 =====
	if (dwProID != eLine_World) return;

	const std::uint32_t world_code = 0;

	// (샘플) 임시 char_id: 실제 서비스에서는 로그인 이후 확정된 char_id를 바인딩해야 함
	const std::uint64_t char_id = (std::uint64_t(dwIndex) << 32) | std::uint64_t(dwSerial);
	{
		std::lock_guard<std::mutex> lk(state_mtx_);
		session_char_ids_[dwIndex] = char_id;
	}
	// 1) Redis에서 상태 로드 시도
	DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;   // 신규 기본값
	st.version = 1;

	if (auto blob = svr::g_Main.TryLoadCharacterState(world_code, char_id))
	{
		DemoCharState loaded{};
		if (TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
		{
			st = loaded;
			spdlog::info("[Demo] loaded from redis char_id={} gold={} ver={}", st.char_id, st.gold, st.version);
		}
		else
		{
			spdlog::warn("[Demo] redis blob invalid, recreate char_id={}", char_id);
		}
	}
	else
	{
		spdlog::info("[Demo] no redis state, create new char_id={}", char_id);
	}

	// 2) 샘플 게임 로직: 접속 보상 gold +10
	st.gold += 10;
	st.version += 1;
	const std::string out_blob = SerializeDemo(st);

	// ✅ 케이스1(Actor당 인스턴스) : PlayerActor 생성/초기화 + 세션 바인딩
	// - 이 람다는 char_id Actor shard에서 실행되므로, PlayerActor 상태를 락 없이 안전하게 만질 수 있다.
	svr::g_Main.PostActor(char_id, [char_id, sid = dwIndex, serial = dwSerial]() {
		auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
		a.bind_session(sid, serial);
		// 기본 전투 스탯(샘플)
		a.combat.hp = 100;
		a.combat.max_hp = 100;
		a.combat.atk = 20;
		a.combat.def = 3;
		a.combat.gold = 1000;
	});

	// 3) Redis 저장 + dirty 마킹 (기존 함수 사용!)
	svr::g_Main.CacheCharacterState(world_code, char_id, out_blob);

	// ✅ 클라에게 "바인딩된 ActorId(=char_id)" 통지 (테스트/벤치용)
	{
		proto::S2C_actor_bound res{};
		res.actor_id = char_id;
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::actor_bound,
			(std::uint16_t)sizeof(res));
		Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&res));
	}
}

void CNetworkEX::CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwSerial;

	// ===== 샘플: 로그아웃 시 즉시 DB 저장(flush_one_char) =====
	if (dwProID == eLine_World)
	{
		const std::uint32_t world_code = 0;
		std::uint64_t char_id = 0;

		{
			std::lock_guard<std::mutex> lk(state_mtx_);
			auto it = session_char_ids_.find(dwIndex);
			if (it != session_char_ids_.end()) {
				char_id = it->second;
				session_char_ids_.erase(it);
			}
		}

		if (char_id != 0) {
			// ✅ Actor 인스턴스 제거(로그아웃)
			svr::g_Main.EraseActor(char_id);
			svr::g_Main.RequestFlushCharacter(world_code, char_id);
		}
	}
	else {
		std::lock_guard<std::mutex> lk(state_mtx_);
	}

	spdlog::info("CNetworkEX::CloseClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

bool CNetworkEX::LoginLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 LoginLineAnalysis 이식
	return false;
}

bool CNetworkEX::WorldLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	const std::uint16_t type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = pMsgHeader->m_wSize - MSG_HEADER_SIZE;

	switch (type)
	{
	case proto::C2SMsg::open_world_notice:
		{
			auto* req = proto::as < proto::C2S_open_world_notice>(pMsg, body_len);
			if (!req) return false;

			std::cout << "[World] recv open_world_notice sid=" << n << "\n";

			//proto::S2C_open_world_success res{};
			//res.ok = 1;
			//auto h = proto::make_header((std::uint16_t)proto::S2CMsg::open_world_success,
			//	(std::uint16_t)sizeof(res));

			//// TcpServer::send(session_id, header, body) 형태라고 가정
			//// (네 TcpServer 시그니처가 다르면 여기 한 줄만 맞추면 됨)
			//server_->send(n, h, reinterpret_cast<const char*>(&res));

			// DB 처리 샘플: DQS로 넘겨서 워커 스레드에서 DB 조회 후 응답 전송
			svr::dqs_payload::OpenWorldNotice payload{};
			payload.sid = n;

			// ✅ 이 요청을 보낸 "그 세션"의 serial을 같이 넣는다
			{
				payload.serial = GetLatestSerial(n);
			}

			copy_cstr(payload.world_name, req->szWorldName);

			const bool pushed = svr::g_Main.PushDQSData(
				(std::uint8_t)svr::dqs::ProcessCode::world,
				(std::uint8_t)svr::dqs::QueryCase::open_world_notice,
				reinterpret_cast<const char*>(&payload),
				(int)sizeof(payload));

			if (!pushed) {
				spdlog::warn("[World] DQS push failed sid={}", n);

				// 샘플: 실패 시 즉시 실패 응답(또는 close 정책 선택)
				proto::S2C_open_world_success res{};
				res.ok = 0;
				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::open_world_success,
					(std::uint16_t)sizeof(res));

				// ✅ 즉시 응답도 serial 체크 send만 사용 (오발송 방지)
				const std::uint32_t serial = GetLatestSerial(n);
				if (serial != 0) {
					Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
				}
				else {
					// serial을 모르면 안전하게 drop(또는 close 정책)
						  // Close(pro_id_, n, 0);
				}

			}
		}
		return true;
	case proto::C2SMsg::add_gold:
		{
			auto* req = proto::as<proto::C2S_add_gold>(pMsg, body_len);
			if (!req) return false;

			const std::uint32_t world_code = 0;

			// 1) 세션에 바인딩된 char_id 조회
			std::uint64_t char_id = 0;
			{
				std::lock_guard<std::mutex> lk(state_mtx_);
				auto it_id = session_char_ids_.find(n);
				if (it_id == session_char_ids_.end())
				{
					spdlog::warn("[Demo] add_gold but no bound char. sid={}", n);
					return true;
				}
				char_id = it_id->second;
			}

			// ✅ 케이스1: PlayerActor 상태(전투 gold)도 같이 올림
			auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
			a.combat.gold += req->add;
			const std::uint32_t combat_gold = a.combat.gold;

			// 2) Redis 상태 로드(없으면 생성)
			DemoCharState st{};
			{
				st.char_id = char_id;
				st.gold = 1000;
				st.version = 1;

				if (auto blob = svr::g_Main.TryLoadCharacterState(world_code, char_id))
				{
					DemoCharState loaded{};
					if (TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
						st = loaded;
				}
			}

			// 3) 샘플 게임 로직: gold 증가
			st.gold += req->add;
			st.version += 1;
			const std::string out_blob = SerializeDemo(st);

			// 4) Redis 저장 + dirty (기존 함수 사용!)
			svr::g_Main.CacheCharacterState(world_code, char_id, out_blob);

			// 5) 응답 전송(serial 체크)
			proto::S2C_add_gold_ok res{};
			res.ok = 1;
			res.gold = combat_gold; // 테스트 편의: 현재 전투 gold 기준

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::add_gold_ok,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
			return true;
		}

	case proto::C2SMsg::get_stats:
		{
			const std::uint64_t char_id = GetActorIdBySession(n);
			auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
			auto cs = a.combat;

			proto::S2C_stats res{};
			res.char_id = char_id;
			res.hp = cs.hp;
			res.max_hp = cs.max_hp;
			res.atk = cs.atk;
			res.def = cs.def;
			res.gold = cs.gold;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
			return true;
		}
	case proto::C2SMsg::heal_self:
		{
			auto* req = proto::as<proto::C2S_heal_self>(pMsg, body_len);
			if (!req) return false;

			const std::uint64_t char_id = GetActorIdBySession(n);
			auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
			auto& st = a.combat;
			if (req->amount == 0) st.hp = st.max_hp;
			else {
				const std::uint64_t nhp = (std::uint64_t)st.hp + (std::uint64_t)req->amount;
				st.hp = (std::uint32_t)std::min<std::uint64_t>(st.max_hp, nhp);
			}
			auto cs = st;

			proto::S2C_stats res{};
			res.char_id = char_id;
			res.hp = cs.hp;
			res.max_hp = cs.max_hp;
			res.atk = cs.atk;
			res.def = cs.def;
			res.gold = cs.gold;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
			return true;
		}
	case proto::C2SMsg::spawn_monster:
		{
			auto* req = proto::as<proto::C2S_spawn_monster>(pMsg, body_len);
			if (!req) return false;

			auto& w = svr::g_Main.GetOrCreateWorldActor();
			svr::MonsterState m{};
			m.id = w.next_monster_id++;
			// template_id에 따라 나중에 테이블로 확장 가능
			if (req->template_id == 1) { m.hp = 120; m.atk = 15; m.def = 4; m.drop_item_id = 2001; m.drop_count = 1; }
			else { m.hp = 50; m.atk = 8; m.def = 1; m.drop_item_id = 1001; m.drop_count = 1; }
			w.monsters[m.id] = m;

			proto::S2C_spawn_monster_ok res{};
			res.monster_id = m.id;
			res.hp = m.hp;
			res.atk = m.atk;
			res.def = m.def;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::spawn_monster_ok,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
			return true;
		}
	case proto::C2SMsg::attack_monster:
		{
			auto* req = proto::as<proto::C2S_attack_monster>(pMsg, body_len);
			if (!req) return false;

			// ✅ 케이스1: (공격자 Actor)에서 스탯을 읽고 -> (월드 Actor)에서 몬스터 상태를 수정
			const std::uint64_t attacker_id = GetActorIdBySession(n);
			auto& attacker = svr::g_Main.GetOrCreatePlayerActor(attacker_id);
			const std::uint32_t attacker_atk = attacker.combat.atk;
			const std::uint32_t attacker_gold = attacker.combat.gold;
			const std::uint32_t sid = n;
			const std::uint32_t serial = GetLatestSerial(n);
			const std::uint64_t monster_id = req->monster_id;

			if (serial == 0) return true;

			// 월드 Actor로 넘김
			svr::g_Main.PostActor(0, [self = shared_from_this(), dwProID, sid, serial, attacker_id, attacker_atk, attacker_gold, monster_id]() mutable {
				auto& w = svr::g_Main.GetOrCreateWorldActor();
				auto it_m = w.monsters.find(monster_id);
				if (it_m == w.monsters.end()) {
					return; // 없는 몬스터 -> 무시
				}

				auto& mon = it_m->second;
				std::uint32_t dmg = (attacker_atk > mon.def) ? (attacker_atk - mon.def) : 1;
				bool killed = false;
				std::uint32_t mon_hp = 0;
				std::uint32_t drop_item = 0;
				std::uint32_t drop_cnt = 0;
				std::uint32_t reward_gold = 0;

				if (dmg >= mon.hp) {
					killed = true;
					mon_hp = 0;
					drop_item = mon.drop_item_id;
					drop_cnt = mon.drop_count;
					reward_gold = 50;
					w.monsters.erase(it_m);
				}
				else {
					mon.hp -= dmg;
					mon_hp = mon.hp;
				}

				// 공격자 보상은 공격자 Actor에서 처리
				if (reward_gold > 0) {
					svr::g_Main.PostActor(attacker_id, [attacker_id, reward_gold]() {
						auto& a = svr::g_Main.GetOrCreatePlayerActor(attacker_id);
						a.combat.gold += reward_gold;
					});
				}

				proto::S2C_attack_result res{};
				res.attacker_id = attacker_id;
				res.target_id = monster_id;
				res.damage = dmg;
				res.target_hp = mon_hp;
				res.killed = killed ? 1u : 0u;
				res.drop_item_id = drop_item;
				res.drop_count = drop_cnt;
				res.attacker_gold = attacker_gold + reward_gold;

				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result,
					(std::uint16_t)sizeof(res));
				self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
			});
			return true;
		}
	case proto::C2SMsg::attack_player:
		{
			auto* req = proto::as<proto::C2S_attack_player>(pMsg, body_len);
			if (!req) return false;

			// 주의: 이 패킷은 ResolveActorIdForPacket에서 target_char_id Actor로 라우팅되지만
			//      dwClientIndex(n)은 "보낸 쪽 세션" 그대로 들어온다.
			// ✅ 이 핸들러는 ResolveActorIdForPacket에서 target_char_id Actor로 라우팅된다.
			//    즉, 현재 스레드는 "타겟 PlayerActor"의 shard다.
			std::uint64_t attacker_id = 0;
			{
				std::lock_guard<std::mutex> lk(state_mtx_);
				auto it_a = session_char_ids_.find(n);
				if (it_a == session_char_ids_.end()) return true;
				attacker_id = it_a->second;
			}

			auto& target = svr::g_Main.GetOrCreatePlayerActor(req->target_char_id);
			const std::uint32_t target_sid = target.sid;

			std::uint32_t dmg = 10; // 샘플 고정 데미지
			const std::uint32_t real_dmg = (dmg > target.combat.def) ? (dmg - target.combat.def) : 1;
			dmg = real_dmg;
			bool killed = false;
			if (real_dmg >= target.combat.hp) {
				target.combat.hp = 0;
				killed = true;
			}
			else {
				target.combat.hp -= real_dmg;
			}
			const std::uint32_t target_hp = target.combat.hp;

			proto::S2C_attack_result res{};
			res.attacker_id = attacker_id;
			res.target_id = req->target_char_id;
			res.damage = dmg;
			res.target_hp = target_hp;
			res.killed = killed ? 1u : 0u;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result,
				(std::uint16_t)sizeof(res));

			// attacker에게도
			const std::uint32_t a_serial = GetLatestSerial(n);
			if (a_serial != 0) {
				Send(dwProID, n, a_serial, h, reinterpret_cast<const char*>(&res));
			}
			// target에게도
			if (target_sid != 0) {
				const std::uint32_t t_serial = GetLatestSerial(target_sid);
				if (t_serial != 0) {
					Send(dwProID, target_sid, t_serial, h, reinterpret_cast<const char*>(&res));
				}
			}

			return true;
		}
	case proto::C2SMsg::actor_seq_test:
		{
			auto* req = proto::as<proto::C2S_actor_seq_test>(pMsg, body_len);
			if (!req) return false;

			const std::uint64_t actor_id = GetActorIdBySession(n);

			// ✅ Actor 단일 실행/순서성 검증용 (shard thread local)
			thread_local std::unordered_map<std::uint64_t, std::uint32_t> last_seq;
			thread_local std::unordered_map<std::uint64_t, bool> in_progress;
			thread_local std::uint32_t error_count = 0;

			bool& busy = in_progress[actor_id];
			if (busy) {
				++error_count; // 같은 actor가 동시에 실행되면 안 됨

			}
			busy = true;

			// 단일 연결(TCP)에서는 seq가 1씩 증가해야 정상
			const std::uint32_t prev = last_seq[actor_id];
			if (req->seq != prev + 1) {
				++error_count;
			}
			last_seq[actor_id] = req->seq;

			if (req->work_us > 0) {
				std::this_thread::sleep_for(std::chrono::microseconds(req->work_us));
			}

			busy = false;

			proto::S2C_actor_seq_ack res{};
			res.ok = 1;
			res.seq = req->seq;
			res.shard = (proto::u32)std::max(0, net::ActorSystem::current_shard_index());
			res.errors = error_count;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::actor_seq_ack,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
			return true;
		}
	case proto::C2SMsg::actor_forward:
		{
			auto* req = proto::as<proto::C2S_actor_forward>(pMsg, body_len);
			if (!req) return false;

			const std::uint64_t actor_id = (req->target_actor_id != 0) ? req->target_actor_id : GetActorIdBySession(n);

			thread_local std::unordered_map<std::uint64_t, bool> in_progress;
			thread_local std::uint32_t error_count = 0;

			bool& busy = in_progress[actor_id];
			if (busy) { ++error_count; }
			busy = true;

			if (req->work_us > 0) {
				std::this_thread::sleep_for(std::chrono::microseconds(req->work_us));
			}

			busy = false;

			// tag를 seq 필드로 돌려서 디버깅
			proto::S2C_actor_seq_ack res{};
			res.ok = 1;
			res.seq = req->tag;
			res.shard = (proto::u32)std::max(0, net::ActorSystem::current_shard_index());
			res.errors = error_count;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::actor_seq_ack,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
 			return true;
 		}
	default:
		std::cout << "[World] unknown type=" << type << "\n";
		return true;
	}
}

bool CNetworkEX::ControlLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 ControlLineAnalysis 이식
	return false;
}
