#include "channel_handler.h"

#include <cstring>
#include <functional>
#include <memory>
#include <iostream>

#include <spdlog/spdlog.h>

#include "core/util/string_utils.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "services/channel/runtime/channel_runtime.h"
#include "services/channel/actors/channel_actors.h"
#include "db/core/dqs_payloads.h"


namespace {
#pragma pack(push, 1)
	struct DemoCharState
	{
		std::uint64_t char_id = 0;
		std::uint32_t gold = 0;
		std::uint32_t version = 0;
	};
#pragma pack(pop)
	static_assert(sizeof(DemoCharState) == 16);

	std::string SerializeDemo(const DemoCharState& s)
	{
		return std::string(reinterpret_cast<const char*>(&s), sizeof(s));
	}

	bool TryDeserializeDemo(const std::string& blob, DemoCharState& out)
	{
		if (blob.size() != sizeof(DemoCharState)) return false;
		std::memcpy(&out, blob.data(), sizeof(out));
		return true;
	}
} // namespace

void ChannelHandler::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("ChannelHandler::AcceptClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);

	// ===== 샘플 플로우는 World 라인에서만 시연 =====
	if (dwProID != eLine_World) return;

	const std::uint32_t world_code = 0;

	// (샘플) 임시 char_id: 실제 서비스에서는 로그인 이후 확정된 char_id를 바인딩해야 함
	const std::uint64_t char_id = (std::uint64_t(dwIndex) << 32) | std::uint64_t(dwSerial);
	svr::g_Main.BindSessionCharId(dwIndex, char_id);
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

		char_id = svr::g_Main.UnbindSessionCharId(dwIndex);

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
		(void)dwProID;
	}

	spdlog::info("ChannelHandler::CloseClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

bool ChannelHandler::HandleWorldOpenWorldNotice(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_open_world_notice>(body, body_len);
	if (!req) return false;

	std::cout << "[World] recv open_world_notice sid=" << sid << "\n";

	// DB 처리 샘플: DQS로 넘겨서 워커 스레드에서 DB 조회 후 응답 전송
	svr::dqs_payload::OpenWorldNotice payload{};
	payload.sid = sid;

	// ✅ 이 요청을 보낸 "그 세션"의 serial을 같이 넣는다
	payload.serial = GetLatestSerial(sid);
	copy_cstr(payload.world_name, req->szWorldName);

	const bool pushed = svr::g_Main.PushDQSData(
		(std::uint8_t)svr::dqs::ProcessCode::world,
		(std::uint8_t)svr::dqs::QueryCase::open_world_notice,
		reinterpret_cast<const char*>(&payload),
		(int)sizeof(payload));

	if (!pushed) {
		spdlog::warn("[World] DQS push failed sid={}", sid);

		proto::S2C_open_world_success res{};
		res.ok = 0;
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::open_world_success,
			(std::uint16_t)sizeof(res));

		const std::uint32_t serial = GetLatestSerial(sid);
		if (serial != 0) {
			Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
		}
	}

	return true;
}

bool ChannelHandler::HandleWorldAddGold(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_add_gold>(body, body_len);
	if (!req) return false;

	const std::uint32_t world_code = 0;
	const std::uint64_t char_id = svr::g_Main.FindCharIdBySession(sid);
	if (char_id == 0)
	{
		spdlog::warn("[Demo] add_gold but no bound char. sid={}", sid);
		return true;
	}

	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	a.combat.gold += req->add;
	const std::uint32_t combat_gold = a.combat.gold;

	DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;
	st.version = 1;

	if (auto blob = svr::g_Main.TryLoadCharacterState(world_code, char_id))
	{
		DemoCharState loaded{};
		if (TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
			st = loaded;
	}

	st.gold += req->add;
	st.version += 1;
	const std::string out_blob = SerializeDemo(st);
	svr::g_Main.CacheCharacterState(world_code, char_id, out_blob);

	proto::S2C_add_gold_ok res{};
	res.ok = 1;
	res.gold = combat_gold;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::add_gold_ok,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}

bool ChannelHandler::HandleWorldGetStats(std::uint32_t dwProID, std::uint32_t sid)
{
	const std::uint64_t char_id = GetActorIdBySession(sid);
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

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}

bool ChannelHandler::HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_heal_self>(body, body_len);
	if (!req) return false;

	const std::uint64_t char_id = GetActorIdBySession(sid);
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

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}
