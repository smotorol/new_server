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
#include "server_common/session/session_key.h"
#include "services/world/actors/world_actors.h"
#include "services/world/common/demo_char_state.h"
#include "services/world/common/string_utils.h"
#include "server_common/session/session_key.h"

namespace pt_w = proto::world;

namespace {

	pt_w::EnterWorldResultCode MapConsumeFailReason(
		svr::ConsumePendingWorldAuthTicketResultKind kind) noexcept
	{
		using K = svr::ConsumePendingWorldAuthTicketResultKind;
		switch (kind) {
		case K::TokenNotFound:
			return pt_w::EnterWorldResultCode::auth_ticket_not_found;
		case K::Expired:
			return pt_w::EnterWorldResultCode::auth_ticket_expired;
		case K::ReplayDetected:
			return pt_w::EnterWorldResultCode::auth_ticket_replayed;
		case K::AccountMismatch:
		case K::CharMismatch:
		case K::LoginSessionMismatch:
		case K::WorldServerMismatch:
			return pt_w::EnterWorldResultCode::auth_ticket_mismatch;
		default:
			return pt_w::EnterWorldResultCode::internal_error;
		}
	}

	pt_w::EnterWorldResultCode MapBindFailReason(
		svr::BindAuthedWorldSessionResultKind kind) noexcept
	{
		switch (kind) {
		case svr::BindAuthedWorldSessionResultKind::InvalidInput:
			return pt_w::EnterWorldResultCode::bind_invalid_input;
		default:
			return pt_w::EnterWorldResultCode::internal_error;
		}
	}

	pt_w::EnterWorldResultCode MapBeginEnterFailReason(
		svr::BeginEnterWorldSessionResultKind kind) noexcept
	{
		switch (kind) {
		case svr::BeginEnterWorldSessionResultKind::AlreadyPending:
			return pt_w::EnterWorldResultCode::enter_already_pending;
		case svr::BeginEnterWorldSessionResultKind::AlreadyInWorld:
			return pt_w::EnterWorldResultCode::already_in_world;
		case svr::BeginEnterWorldSessionResultKind::Closing:
			return pt_w::EnterWorldResultCode::session_closing;
		case svr::BeginEnterWorldSessionResultKind::InvalidInput:
		default:
			return pt_w::EnterWorldResultCode::internal_error;
		}
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
	auto* req = proto::as<pt_w::C2S_enter_world_with_token>(body, body_len);
	if (!req) {
		spdlog::error("HandleEnterWorldWithToken invalid packet sid={}", n);
		return false;
	}

	pt_w::S2C_enter_world_result res{};
	res.account_id = req->account_id;
	res.char_id = req->char_id;

	const auto serial = GetLatestSerial(n);
	const auto begin_result = runtime().TryBeginEnterWorldSession(
		n,
		serial,
		req->account_id,
		req->char_id);

	if (!begin_result.started()) {
		res.ok = 0;
		res.reason = static_cast<std::uint16_t>(MapBeginEnterFailReason(begin_result.kind));
		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
		spdlog::warn(
			"HandleEnterWorldWithToken rejected by enter-state machine. sid={} serial={} account_id={} char_id={} begin_kind={} stage={}",
			n,
			serial,
			req->account_id,
			req->char_id,
			static_cast<int>(begin_result.kind),
			static_cast<int>(begin_result.stage));
		return true;
	}

	spdlog::info(
		"HandleEnterWorldWithToken request sid={} serial={} account_id={} char_id={} login_session={} token={}",
		n,
		serial,
		req->account_id,
		req->char_id,
		req->login_session,
		req->world_token);

	if (!runtime().RequestConsumeWorldAuthTicket(
		n,
		serial,
		req->account_id,
		req->char_id,
		req->login_session,
		req->world_token))
	{
		res.ok = 0;
		res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::internal_error);
		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
		runtime().CancelPendingEnterWorldSession(n, serial, req->char_id);
		return true;
	}


	spdlog::info(
		"HandleEnterWorldWithToken consume requested. sid={} serial={} account_id={} char_id={} login_session={} token={}",
		n,
		serial,
		req->account_id,
		req->char_id,
		req->login_session,
		req->world_token);

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

	runtime().HandleWorldSessionClosed(dwIndex, dwSerial);

 	spdlog::info(
		"WorldHandler::OnLineClosed forwarded to runtime. sid={} serial={}",
 		dwIndex,
		dwSerial);
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

	svr::demo::DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;
	st.version = 1;

	if (auto blob = runtime().TryLoadCharacterState(world_code, char_id))
	{
		svr::demo::DemoCharState loaded{};
		if (svr::demo::TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)

			st = loaded;
	}

	st.gold += req->add;
	st.version += 1;
	const std::string out_blob = svr::demo::SerializeDemo(st);
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
