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
	// 몬스터 관련은 "월드 Actor(0)"에서만 처리하여 공유 상태(monsters_)를 단일 실행으로 보장
	if (type == (std::uint16_t)proto::C2SMsg::spawn_monster ||
		type == (std::uint16_t)proto::C2SMsg::attack_monster)
	{
		return 0; // world actor
	}

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
		char_id_to_sid_[char_id] = dwIndex;

		// 기본 전투 스탯(샘플) - 나중에 DB/Redis에 붙이면 됨
		CharCombatState cs{};
		cs.hp = 100;
		cs.max_hp = 100;
		cs.atk = 20;
		cs.def = 3;
		cs.gold = 1000;
		session_char_combat_[dwIndex] = cs;
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
	{ std::lock_guard<std::mutex> lk(state_mtx_); session_char_blob_[dwIndex] = out_blob; }

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
				char_id_to_sid_.erase(char_id);
				session_char_ids_.erase(it);
			}
			session_char_blob_.erase(dwIndex);
			session_char_combat_.erase(dwIndex);
		}

		if (char_id != 0) {
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
			std::lock_guard<std::mutex> lk(state_mtx_);
			auto* req = proto::as<proto::C2S_add_gold>(pMsg, body_len);
			if (!req) return false;

			const std::uint32_t world_code = 0;

			// 1) 세션에 바인딩된 char_id 조회
			auto it_id = session_char_ids_.find(n);
			if (it_id == session_char_ids_.end())
			{
				spdlog::warn("[Demo] add_gold but no bound char. sid={}", n);
				return true;
			}
			const std::uint64_t char_id = it_id->second;

			// 2) 메모리(세션) 상태 로드
			DemoCharState st{};
			auto it_blob = session_char_blob_.find(n);
			if (it_blob != session_char_blob_.end())
			{
				(void)TryDeserializeDemo(it_blob->second, st);
			}
			if (st.char_id != char_id)
			{
				// 세션 상태가 없으면(또는 깨졌으면) Redis에서 재로드 시도 후 없으면 생성
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
			session_char_blob_[n] = out_blob;

			// 4) Redis 저장 + dirty (기존 함수 사용!)
			svr::g_Main.CacheCharacterState(world_code, char_id, out_blob);

			// 5) 응답 전송(serial 체크)
			proto::S2C_add_gold_ok res{};
			res.ok = 1;
			res.gold = st.gold;

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
			std::uint64_t char_id = 0;
			CharCombatState cs{};
			{
				std::lock_guard<std::mutex> lk(state_mtx_);
				auto it_id = session_char_ids_.find(n);
				if (it_id == session_char_ids_.end()) return true;
				char_id = it_id->second;
				auto it_cs = session_char_combat_.find(n);
				if (it_cs != session_char_combat_.end()) cs = it_cs->second;
			}

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

			std::uint64_t char_id = 0;
			CharCombatState cs{};
			{
				std::lock_guard<std::mutex> lk(state_mtx_);
				auto it_id = session_char_ids_.find(n);
				if (it_id == session_char_ids_.end()) return true;
				char_id = it_id->second;

				auto& st = session_char_combat_[n]; // 없으면 생성
				if (req->amount == 0) st.hp = st.max_hp;
				else {
					const std::uint64_t nhp = (std::uint64_t)st.hp + (std::uint64_t)req->amount;
					st.hp = (std::uint32_t)std::min<std::uint64_t>(st.max_hp, nhp);
				}
				cs = st;
			}

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

			MonsterState m{};
			{
				std::lock_guard<std::mutex> lk(state_mtx_);
				m.id = next_monster_id_++;
				// template_id에 따라 나중에 테이블로 확장 가능
				if (req->template_id == 1) { m.hp = 120; m.atk = 15; m.def = 4; m.drop_item_id = 2001; m.drop_count = 1; }
				else { m.hp = 50; m.atk = 8; m.def = 1; m.drop_item_id = 1001; m.drop_count = 1; }
				monsters_[m.id] = m;
			}

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

			std::uint64_t attacker_id = 0;
			std::uint32_t attacker_gold = 0;
			std::uint32_t attacker_atk = 0;

			MonsterState mon{};
			bool found = false;
			bool killed = false;
			std::uint32_t dmg = 0;
			std::uint32_t mon_hp = 0;
			std::uint32_t drop_item = 0;
			std::uint32_t drop_cnt = 0;

			{
				std::lock_guard<std::mutex> lk(state_mtx_);

				auto it_id = session_char_ids_.find(n);
				if (it_id == session_char_ids_.end()) return true;
				attacker_id = it_id->second;

				auto it_cs = session_char_combat_.find(n);
				if (it_cs != session_char_combat_.end()) {
					attacker_atk = it_cs->second.atk;
					attacker_gold = it_cs->second.gold;
				}

				auto it_m = monsters_.find(req->monster_id);
				if (it_m == monsters_.end()) {
					found = false;
				}
				else {
					found = true;
					mon = it_m->second;
					dmg = (attacker_atk > mon.def) ? (attacker_atk - mon.def) : 1;
					if (dmg >= mon.hp) {
						killed = true;
						mon.hp = 0;
						drop_item = mon.drop_item_id;
						drop_cnt = mon.drop_count;
						mon_hp = 0;
						monsters_.erase(it_m);

						// 샘플 보상: 골드 +50
						auto& cs = session_char_combat_[n];
						cs.gold += 50;
						attacker_gold = cs.gold;
					}
					else {
						mon.hp -= dmg;
						mon_hp = mon.hp;
						it_m->second.hp = mon.hp;
					}
				}
			}

			if (!found) {
				// 없는 몬스터 -> 무시
				return true;
			}

			proto::S2C_attack_result res{};
			res.attacker_id = attacker_id;
			res.target_id = req->monster_id;
			res.damage = dmg;
			res.target_hp = mon_hp;
			res.killed = killed ? 1u : 0u;
			res.drop_item_id = drop_item;
			res.drop_count = drop_cnt;
			res.attacker_gold = attacker_gold;

			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result,
				(std::uint16_t)sizeof(res));

			const std::uint32_t serial = GetLatestSerial(n);
			if (serial != 0) {
				Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
			}
			return true;
		}
	case proto::C2SMsg::attack_player:
		{
			auto* req = proto::as<proto::C2S_attack_player>(pMsg, body_len);
			if (!req) return false;

			// 주의: 이 패킷은 ResolveActorIdForPacket에서 target_char_id Actor로 라우팅되지만
			//      dwClientIndex(n)은 "보낸 쪽 세션" 그대로 들어온다.
			std::uint64_t attacker_id = 0;
			std::uint32_t dmg = 10; // 샘플 고정 데미지
			std::uint32_t target_hp = 0;
			bool killed = false;

			std::uint32_t target_sid = 0;

			{
				std::lock_guard<std::mutex> lk(state_mtx_);
				auto it_a = session_char_ids_.find(n);
				if (it_a == session_char_ids_.end()) return true;
				attacker_id = it_a->second;

				auto it_t = char_id_to_sid_.find(req->target_char_id);
				if (it_t == char_id_to_sid_.end()) return true;
				target_sid = it_t->second;

				auto& tcs = session_char_combat_[target_sid];
				const std::uint32_t real_dmg = (dmg > tcs.def) ? (dmg - tcs.def) : 1;
				dmg = real_dmg;
				if (real_dmg >= tcs.hp) {
					tcs.hp = 0;
					killed = true;
				}
				else {
					tcs.hp -= real_dmg;
				}
				target_hp = tcs.hp;
			}

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
			const std::uint32_t t_serial = GetLatestSerial(target_sid);
			if (t_serial != 0) {
				Send(dwProID, target_sid, t_serial, h, reinterpret_cast<const char*>(&res));
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
