#include "channel_handler.h"
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <thread>
#include <functional>
#include <memory>
#include "core/util/string_utils.h"
#include "net/actor/actor_system.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "services/channel/runtime/channel_runtime.h"
#include "services/channel/actors/channel_actors.h"
#include "db/core/dqs_payloads.h"


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

std::uint64_t ChannelHandler::GetActorIdBySession(std::uint32_t sid) const
{
	std::lock_guard lk(state_mtx_);
	auto it = session_char_ids_.find(sid);
	if (it != session_char_ids_.end() && it->second != 0)
		return it->second;
	return static_cast<std::uint64_t>(sid);
}

std::uint64_t ChannelHandler::ResolveActorId(std::uint32_t session_idx) const
{
	// World 라인은 (sid -> char_id) 바인딩 이후 char_id Actor로 라우팅
	return GetActorIdBySession(session_idx);
}

std::uint64_t ChannelHandler::ResolveActorIdForPacket(std::uint32_t session_idx,
	const _MSG_HEADER& header, const char* body, std::size_t body_len,
	std::uint64_t default_actor) const
{
	(void)session_idx;
	const std::uint16_t type = proto::get_type_u16(header);
	if (type == (std::uint16_t)proto::C2SMsg::actor_forward) {
		auto* req = proto::as<proto::C2S_actor_forward>(body, body_len);
		if (req && req->target_actor_id != 0) return req->target_actor_id;
	}

	// PvP는 "맞는 쪽(target_char_id)" Actor로 라우팅해서 타겟 HP를 단일 실행으로 갱신
	if (type == (std::uint16_t)proto::C2SMsg::attack_player) {
		auto* req = proto::as<proto::C2S_attack_player>(body, body_len);
		if (req && req->target_char_id != 0) return req->target_char_id;
	}

	return default_actor;
}
bool ChannelHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg)
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

void ChannelHandler::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("ChannelHandler::AcceptClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);

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
		// zone init
		a.zone_id = 1;
		a.pos = { 0,0 };
		svr::g_Main.PostActor(svr::MakeZoneActorId(a.zone_id), [char_id, sid, serial]() {
			auto& z = svr::g_Main.GetOrCreateZoneActor(1);
			z.JoinOrUpdate(char_id, { 0,0 }, sid, serial);
		});
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

void ChannelHandler::CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	// ✅ 늦게 도착한 disconnect(세션 재사용/재접속) 방지
	// - 최신 serial이 아니면 이미 다른 세션이 같은 index를 쓰고 있는 것
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug("CloseClientCheck ignored (stale). pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
		return;
	}

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
			// ✅ 중요:
			// HandleDisconnected_ -> CloseClientCheck 는 io 스레드에서 호출될 수 있다.
			// ZoneActor 내부 컨테이너(players/cells)에 여기서 직접 접근하면 data race로 크래시가 난다.
			// => 반드시 ActorSystem(PostActor)로 넘겨서, 각 Actor 소유 샤드에서만 상태를 바꾼다.

			auto self = shared_from_this(); // shared_ptr<dc::NetworkEXBase>

			proto::S2C_player_despawn despawn{};
			despawn.char_id = char_id;
			auto despawn_h = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn,
				(std::uint16_t)sizeof(despawn));

			svr::g_Main.PostActor(char_id, [self, dwProID, world_code, char_id, despawn, despawn_h]() mutable {
				// 1) PlayerActor: 오프라인 마킹 + zone_id 확보
				auto& p = svr::g_Main.GetOrCreatePlayerActor(char_id);
				const std::uint32_t zone_id = p.zone_id;
				p.online = false;
				p.sid = 0;
				p.serial = 0;

				// 2) ZoneActor: Leave + neighbors 수집
				const std::uint64_t zid = svr::MakeZoneActorId(zone_id);
				svr::g_Main.PostActor(zid, [self, dwProID, zone_id, char_id, despawn, despawn_h]() mutable {
					auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);

					// 중복 disconnect 등 멱등 처리
					auto itp = z.players.find(char_id);
					if (itp == z.players.end()) {
						z.Leave(char_id);
						return;
					}

					const auto neigh = z.GatherNeighborsVec(itp->second.cx, itp->second.cy);
					z.Leave(char_id);

					// 3) 수신자 Actor에서 세션 체크 후 send
					for (auto other_id : neigh) {
						if (other_id == char_id) continue;
						auto it = z.players.find(other_id);
						if (it == z.players.end()) continue;
						if (it->second.sid == 0 || it->second.serial == 0) continue;
						self->Send(dwProID, it->second.sid, it->second.serial, despawn_h, reinterpret_cast<const char*>(&despawn));
					}
				});

				// 4) Actor 제거 + DB flush (PlayerActor 샤드)
				svr::g_Main.EraseActor(char_id);
				svr::g_Main.RequestFlushCharacter(world_code, char_id);
			});
		}

	}
	else {
		std::lock_guard<std::mutex> lk(state_mtx_);
	}

	spdlog::info("ChannelHandler::CloseClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

bool ChannelHandler::LoginLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 LoginLineAnalysis 이식
	return false;
}

bool ChannelHandler::WorldLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
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

	case proto::C2SMsg::move:
		{
			auto* req = proto::as<proto::C2S_move>(pMsg, body_len);
			if (!req) return false;

			const std::uint64_t char_id = GetActorIdBySession(n);
			auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
			const std::uint32_t zone_id = a.zone_id;
			a.pos = { req->x, req->y };

			const std::uint32_t sid = n;
			const std::uint32_t serial = GetLatestSerial(n);
			if (serial == 0) return true;
			auto self = shared_from_this();
			auto* th = this;

			svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [self, th, dwProID, sid, serial, zone_id, char_id, nx = req->x, ny = req->y]() {
				auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
				auto diff = z.Move(char_id, { nx, ny }, sid, serial);

				std::vector<std::uint64_t> entered;
				std::vector<std::uint64_t> exited;
				{
					auto oldv = diff.old_vis;
					auto newv = diff.new_vis;
					std::sort(oldv.begin(), oldv.end());
					std::sort(newv.begin(), newv.end());
					entered.reserve(newv.size());
					exited.reserve(oldv.size());
					std::set_difference(newv.begin(), newv.end(), oldv.begin(), oldv.end(), std::back_inserter(entered));
					std::set_difference(oldv.begin(), oldv.end(), newv.begin(), newv.end(), std::back_inserter(exited));
				}

				// to self: spawn entered / despawn exited
				for (auto oid : entered) {
					auto itp = z.players.find(oid);
					if (itp == z.players.end()) continue;
					proto::S2C_player_spawn smsg{};
					smsg.char_id = oid;
					smsg.x = itp->second.pos.x;
					smsg.y = itp->second.pos.y;
					auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn, (std::uint16_t)sizeof(smsg));
					self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&smsg));
				}
				for (auto oid : exited) {
					proto::S2C_player_despawn dmsg{};
					dmsg.char_id = oid;
					auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn, (std::uint16_t)sizeof(dmsg));
					self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&dmsg));
				}

				// broadcast spawn/despawn
				proto::S2C_player_spawn self_spawn{};
				self_spawn.char_id = char_id;
				self_spawn.x = nx;
				self_spawn.y = ny;
				auto h_spawn = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn, (std::uint16_t)sizeof(self_spawn));

				// ✅ (ZoneActor) enqueue broadcast messages (budgeted flush)
				auto body_self_spawn = svr::ZoneActor::MakeBody_(self_spawn);

				proto::S2C_player_despawn self_des{};
				self_des.char_id = char_id;
				auto h_des = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn, (std::uint16_t)sizeof(self_des));
				auto body_self_des = svr::ZoneActor::MakeBody_(self_des);

				for (auto rid : entered) {
					auto it = z.players.find(rid);
					if (it == z.players.end()) continue;
					if (it->second.sid == 0 || it->second.serial == 0) continue;
					z.EnqueueSend_(it->second.sid, it->second.serial, h_spawn, body_self_spawn, (std::uint16_t)proto::S2CMsg::player_spawn);
				}
				for (auto rid : exited) {
					auto it = z.players.find(rid);
					if (it == z.players.end()) continue;
					if (it->second.sid == 0 || it->second.serial == 0) continue;
					z.EnqueueSend_(it->second.sid, it->second.serial, h_des, body_self_des, (std::uint16_t)proto::S2CMsg::player_despawn);
				}

				// broadcast move to all in new vis
				proto::S2C_player_move mmsg{};
				mmsg.char_id = char_id;
				mmsg.x = nx;
				mmsg.y = ny;
				auto h_move = proto::make_header((std::uint16_t)proto::S2CMsg::player_move, (std::uint16_t)sizeof(mmsg));
				auto body_move = svr::ZoneActor::MakeBody_(mmsg);

				for (auto rid : diff.new_vis) {
					auto it = z.players.find(rid);
					if (it == z.players.end()) continue;
					if (it->second.sid == 0 || it->second.serial == 0) continue;
					z.EnqueueSend_(it->second.sid, it->second.serial, h_move, body_move, (std::uint16_t)proto::S2CMsg::player_move, char_id);
				}

				// ✅ flush budgeted pending sends for this zone job
				z.FlushPendingSends_(static_cast<ChannelHandler&>(*self), dwProID);

				// ✅ if still pending (slow receivers), keep draining with small follow-up jobs
				if (z.HasPendingNet_()) {
					auto flusher = std::make_shared<std::function<void(int)>>();
					*flusher = [self, dwProID, zone_id, flusher](int depth) {
						auto& zf = svr::g_Main.GetOrCreateZoneActor(zone_id);
						zf.FlushPendingSends_(static_cast<ChannelHandler&>(*self), dwProID, 1500, 1 * 1024 * 1024);
						if (depth < 16 && zf.HasPendingNet_()) {
							svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [flusher, depth]() { (*flusher)(depth + 1); });
						}
					};
					svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [flusher]() { (*flusher)(0); });
				}

			});
			return true;
		}

	case proto::C2SMsg::bench_move:
		{
			auto* req = proto::as<proto::C2S_bench_move>(pMsg, body_len);
			if (!req) return false;

			// server-side bench metric: C2S bench_move receive count
			svr::g_c2s_bench_move_rx.fetch_add(1, std::memory_order_relaxed);

			const std::uint64_t char_id = GetActorIdBySession(n);
			auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
			const std::uint32_t zone_id = a.zone_id;
			a.pos = { req->x, req->y };

			const std::uint32_t sid = n;
			const std::uint32_t serial = GetLatestSerial(n);
			if (serial == 0) return true;
			auto self = shared_from_this();
			auto* th = this;

			// ZoneActor에서 이동/브로드캐스트까지 끝낸 뒤 ack를 쏴서 RTT 측정이 의미 있게 만든다.
			svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id),
				[self, th, dwProID, sid, zone_id, char_id,
				seq = req->seq, work_us = req->work_us, nx = req->x, ny = req->y, cts = req->client_ts_ns]() {
				const std::uint32_t serial = th->GetLatestSerial(sid);

				auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);

				// ✅ Bench fast path:
				// - old/new visible diff(정렬/차집합) 및 spawn/despawn 브로드캐스트를 생략
				// - Move는 tick buffer에 last-write-wins로 모은 뒤, player_move_batch로 1회 전송
				if (serial != 0) {
					z.MoveFastUpdate(char_id, { nx, ny }, sid, serial);
					z.EnqueueBenchAck(sid, serial, seq, cts, zone_id);
				}

				if (work_us > 0) {
					std::this_thread::sleep_for(std::chrono::microseconds(work_us));
				}


				// ✅ tick flush (if due)
				z.FlushMoveTickIfDue_(static_cast<ChannelHandler&>(*self), dwProID, false);
			});
			return true;
		}

	case proto::C2SMsg::bench_reset:
		{
			// ✅ client bench_reset와 서버 bench counters 리셋을 동기화
			svr::g_Main.RequestBenchReset();
			return true;
		}
	case proto::C2SMsg::bench_measure:
		{
			auto* req = proto::as<proto::C2S_bench_measure>(pMsg, body_len);
			if (!req) return false;
			const int seconds = (int)std::max<proto::u32>(1, req->seconds);
			svr::g_Main.RequestBenchMeasure(seconds);
			return true;
		}

	case proto::C2SMsg::spawn_monster:
		{
			auto* req = proto::as<proto::C2S_spawn_monster>(pMsg, body_len);
			if (!req) return false;

			const std::uint64_t char_id = GetActorIdBySession(n);
			auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
			const std::uint32_t zone_id = a.zone_id;
			const std::uint32_t sid = n;
			const std::uint32_t serial = GetLatestSerial(n);
			if (serial == 0) return true;

			auto self = shared_from_this();
			svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [self, dwProID, sid, serial, zone_id, tid = req->template_id]() {
				auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
				svr::MonsterState m{};
				m.id = z.next_monster_id++;
				if (tid == 1) { m.hp = 120; m.atk = 15; m.def = 4; m.drop_item_id = 2001; m.drop_count = 1; }
				else { m.hp = 50; m.atk = 8; m.def = 1; m.drop_item_id = 1001; m.drop_count = 1; }
				z.monsters[m.id] = m;

				proto::S2C_spawn_monster_ok res{};
				res.monster_id = m.id;
				res.hp = m.hp;
				res.atk = m.atk;
				res.def = m.def;
				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::spawn_monster_ok, (std::uint16_t)sizeof(res));
				self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
			});
			return true;
		}
	case proto::C2SMsg::attack_monster:
		{
			auto* req = proto::as<proto::C2S_attack_monster>(pMsg, body_len);
			if (!req) return false;

			const std::uint64_t attacker_id = GetActorIdBySession(n);
			auto& attacker = svr::g_Main.GetOrCreatePlayerActor(attacker_id);
			const std::uint32_t attacker_atk = attacker.combat.atk;
			const std::uint32_t zone_id = attacker.zone_id;
			const std::uint32_t sid = n;
			const std::uint32_t serial = GetLatestSerial(n);
			if (serial == 0) return true;
			const std::uint64_t monster_id = req->monster_id;
			auto self = shared_from_this();

			svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [self, dwProID, sid, serial, attacker_id, attacker_atk, zone_id, monster_id]() {
				auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
				auto it_m = z.monsters.find(monster_id);
				if (it_m == z.monsters.end()) return;

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
					z.monsters.erase(it_m);
				}
				else {
					mon.hp -= dmg;
					mon_hp = mon.hp;
				}

				if (!killed) {
					proto::S2C_attack_result res{};
					res.attacker_id = attacker_id;
					res.target_id = monster_id;
					res.damage = dmg;
					res.target_hp = mon_hp;
					res.killed = 0;
					auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result, (std::uint16_t)sizeof(res));
					self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
					return;
				}

				const std::uint64_t tx_id = z.next_tx_id++;
				svr::g_Main.PostActor(attacker_id, [self, dwProID, sid, serial, attacker_id, monster_id, dmg, mon_hp, drop_item, drop_cnt, reward_gold, tx_id]() {
					auto& a = svr::g_Main.GetOrCreatePlayerActor(attacker_id);
					bool ok = a.CanAddItem(drop_item, drop_cnt);
					if (ok) {
						a.CommitLoot(tx_id, drop_item, drop_cnt, reward_gold);
					}

					proto::S2C_attack_result res{};
					res.attacker_id = attacker_id;
					res.target_id = monster_id;
					res.damage = dmg;
					res.target_hp = mon_hp;
					res.killed = 1;
					res.drop_item_id = ok ? drop_item : 0;
					res.drop_count = ok ? drop_cnt : 0;
					res.attacker_gold = a.combat.gold;

					auto h = proto::make_header((std::uint16_t)proto::S2CMsg::attack_result, (std::uint16_t)sizeof(res));
					self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
				});
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

bool ChannelHandler::ControlLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 ControlLineAnalysis 이식
	return false;
}
