#include "services/world/handler/world_handler.h"

#include <cstring>
#include <functional>
#include <memory>
#include <iostream>

#include <spdlog/spdlog.h>

#include "core/util/string_utils.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/client/world_proto.h"
#include "db/core/dqs_payloads.h"
#include "services/world/actors/world_actors.h"

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

void WorldHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info("WorldHandler::OnWorldAccepted index={} serial={}", dwIndex, dwSerial);

	// 더 이상 여기서 임시 char_id를 만들지 않는다.
	// 실제 char_id 바인딩은 enter_world_with_token 성공 후에만 수행한다.
}

bool WorldHandler::HandleEnterWorldWithToken(
	std::uint32_t dwProID,
	std::uint32_t n,
	const char* body,
	std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_enter_world_with_token>(body, body_len);
	if (!req) {
		spdlog::error("HandleEnterWorldWithToken invalid packet sid={}", n);
		return false;
	}

	proto::S2C_enter_world_result res{};
	res.account_id = req->account_id;
	res.char_id = req->char_id;

	const auto serial = GetLatestSerial(n);

	if (!runtime().ConsumePendingWorldAuthTicket(
		req->account_id,
		req->char_id,
		req->world_token))
	{
		res.ok = 0;
		const auto h = proto::make_header(
			static_cast<std::uint16_t>(proto::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));

		spdlog::warn(
			"HandleEnterWorldWithToken denied sid={} serial={} account_id={} char_id={}",
			n, serial, req->account_id, req->char_id);
		return true;
	}

	const std::uint32_t world_code = 0;
	const std::uint64_t char_id = req->char_id;

	runtime().BindSessionCharId(n, char_id);

	DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;
	st.version = 1;

	if (auto blob = runtime().TryLoadCharacterState(world_code, char_id))
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

	st.gold += 10;
	st.version += 1;
	const std::string out_blob = SerializeDemo(st);

	runtime().PostActor(char_id, [this, char_id, sid = n, serial]() {
		auto& a = runtime().GetOrCreatePlayerActor(char_id);
		a.bind_session(sid, serial);
		a.combat.hp = 100;
		a.combat.max_hp = 100;
		a.combat.atk = 20;
		a.combat.def = 3;
		a.combat.gold = 1000;
		a.zone_id = 1;
		a.pos = { 0,0 };

		runtime().PostActor(svr::MakeZoneActorId(a.zone_id), [this, char_id, sid, serial]() {
			auto& z = runtime().GetOrCreateZoneActor(1);
			z.JoinOrUpdate(char_id, { 0,0 }, sid, serial);
		});
	});

	runtime().CacheCharacterState(world_code, char_id, out_blob);

	{
		proto::S2C_actor_bound bound{};
		bound.actor_id = char_id;
		auto h = proto::make_header(
			static_cast<std::uint16_t>(proto::S2CMsg::actor_bound),
			static_cast<std::uint16_t>(sizeof(bound)));
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&bound));
	}

	{
		res.ok = 1;
		auto h = proto::make_header(
			static_cast<std::uint16_t>(proto::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
	}

	spdlog::info(
		"HandleEnterWorldWithToken success sid={} serial={} account_id={} char_id={}",
		n, serial, req->account_id, req->char_id);

	return true;
}

bool WorldHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug("WorldHandler::OnWorldDisconnected ignored (stale). index={} serial={}", dwIndex, dwSerial);
		return false;
	}
	return true;
}

void WorldHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	// ✅ 늦게 도착한 disconnect(세션 재사용/재접속) 방지
	// - 최신 serial이 아니면 이미 다른 세션이 같은 index를 쓰고 있는 것
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug("OnWorldDisconnected ignored (stale). index={} serial={}", dwIndex, dwSerial);
		return;
	}

	// ===== 샘플: 로그아웃 시 즉시 DB 저장(flush_one_char) =====
	{
		const std::uint32_t world_code = 0;
		std::uint64_t char_id = 0;

		char_id = runtime().UnbindSessionCharId(dwIndex);

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

			runtime().PostActor(char_id, [this, self, world_code, char_id, despawn, despawn_h]() mutable {
				// 1) PlayerActor: 오프라인 마킹 + zone_id 확보
				auto& p = runtime().GetOrCreatePlayerActor(char_id);
				const std::uint32_t zone_id = p.zone_id;
				p.online = false;
				p.sid = 0;
				p.serial = 0;

				// 2) ZoneActor: Leave + neighbors 수집
				const std::uint64_t zid = svr::MakeZoneActorId(zone_id);
				runtime().PostActor(zid, [this, self, zone_id, char_id, despawn, despawn_h]() mutable {
					auto& z = runtime().GetOrCreateZoneActor(zone_id);

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
						self->Send(0, it->second.sid, it->second.serial, despawn_h, reinterpret_cast<const char*>(&despawn));
					}
				});

				// 4) Actor 제거 + DB flush (PlayerActor 샤드)
				runtime().EraseActor(char_id);
				runtime().RequestFlushCharacter(world_code, char_id);
			});
		}

	}

	spdlog::info("WorldHandler::OnWorldDisconnected index={} serial={}", dwIndex, dwSerial);
}

bool WorldHandler::HandleWorldOpenWorldNotice(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
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

	const bool pushed = runtime().PushDQSData(
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

bool WorldHandler::HandleWorldAddGold(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_add_gold>(body, body_len);
	if (!req) return false;

	const std::uint32_t world_code = 0;
	const std::uint64_t char_id = runtime().FindCharIdBySession(sid);
	if (char_id == 0)
	{
		spdlog::warn("[Demo] add_gold but no bound char. sid={}", sid);
		return true;
	}

	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	a.combat.gold += req->add;
	const std::uint32_t combat_gold = a.combat.gold;

	DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;
	st.version = 1;

	if (auto blob = runtime().TryLoadCharacterState(world_code, char_id))
	{
		DemoCharState loaded{};
		if (TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
			st = loaded;
	}

	st.gold += req->add;
	st.version += 1;
	const std::string out_blob = SerializeDemo(st);
	runtime().CacheCharacterState(world_code, char_id, out_blob);

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

bool WorldHandler::HandleWorldGetStats(std::uint32_t dwProID, std::uint32_t sid)
{
	const std::uint64_t char_id = GetActorIdBySession(sid);
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
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

bool WorldHandler::HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_heal_self>(body, body_len);
	if (!req) return false;

	const std::uint64_t char_id = GetActorIdBySession(sid);
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
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
