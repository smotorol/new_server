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
	runtime().ReplaceWorldSessionForChar(char_id, n, serial);

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
		spdlog::debug(
			"WorldHandler stale close ignored. index={} serial={}",
			dwIndex, dwSerial);
		return false;
	}
	return true;
}

void WorldHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;

	const auto char_id = runtime().FindCharIdBySession(dwIndex);
	if (char_id != 0) {
		runtime().RemoveWorldSessionBinding(char_id, dwIndex, dwSerial);
	}

	const auto unbound_char_id = runtime().UnbindSessionCharId(dwIndex);

	if (unbound_char_id != 0) {
		runtime().PostActor(unbound_char_id, [this, unbound_char_id, sid = dwIndex, serial = dwSerial]() {
			auto& a = runtime().GetOrCreatePlayerActor(unbound_char_id);

			if (a.sid == sid && a.serial == serial) {
				a.bind_session(0, 0);
			}

			if (a.zone_id != 0) {
				const auto zone_id = a.zone_id;
				runtime().PostActor(svr::MakeZoneActorId(zone_id), [this, unbound_char_id, zone_id]() {
					auto& z = runtime().GetOrCreateZoneActor(zone_id);
					z.Leave(unbound_char_id);
				});
			}
		});
	}

	spdlog::info(
		"WorldHandler::OnWorldDisconnected index={} serial={} char_id={}",
		dwIndex, dwSerial, unbound_char_id);
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
