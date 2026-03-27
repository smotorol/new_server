#include "services/world/handler/world_handler.h"

#include <cstring>
#include <algorithm>
#include <fmt/format.h>
#include <functional>
#include <memory>
#include <iostream>

#include <spdlog/spdlog.h>

#include "core/util/string_utils.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/client/world_proto.h"
#include "db/core/dqs_payloads.h"
#include "server_common/log/enter_flow_log.h"
#include "server_common/session/session_key.h"
#include "services/world/actors/world_actors.h"
#include "services/world/common/demo_char_state.h"
#include "services/world/common/string_utils.h"
#include "services/world/common/character_runtime_hot_state.h"
#include "services/world/runtime/world_runtime.h"
#include "services/world/db/item_template_repository.h"
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

bool WorldHandler::HandleEnterWorldWithToken(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len)
{
	auto* req = proto::as<pt_w::C2S_enter_world_with_token>(body, body_len);
	if (!req) return false;

	const std::uint32_t serial = GetLatestSerial(n);
	dc::enterlog::LogEnterFlow(
		spdlog::level::info,
		dc::enterlog::EnterStage::ClientWorldEnterRequestReceived,
		{ 0, req->account_id, 0, n, serial, req->login_session, req->world_token },
		{},
		req->char_id != 0 ? "client_char_id_ignored" : "");

	if (serial == 0) {
		spdlog::warn(
			"HandleEnterWorldWithToken ignored because session serial is stale. sid={} account_id={}",
			n,
			req->account_id);
		return true;
	}

	if (!runtime().RequestConsumeWorldAuthTicket(
		n,
		serial,
		0,
		req->account_id,
		req->login_session,
		req->world_token)) {
		pt_w::S2C_enter_world_result res{};
		res.ok = 0;
		res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::internal_error);
		res.account_id = req->account_id;
		res.char_id = 0;
		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&res));
		dc::enterlog::LogEnterFlow(
			spdlog::level::warn,
			dc::enterlog::EnterStage::EnterFlowAborted,
			{ 0, req->account_id, 0, n, serial, req->login_session, req->world_token },
			"world_consume_request_send_failed");
		return true;
	}

	spdlog::info(
		"HandleEnterWorldWithToken consume requested. sid={} serial={} account_id={} login_session={} token={}",
		n,
		serial,
		req->account_id,
		req->login_session,
		req->world_token);

	return true;
}

void WorldHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info(
		"WorldHandler::OnLineAccepted sid={} serial={}",
		dwIndex,
		dwSerial);
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
	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("add_gold", sid, char_id)) {
		return true;
	}

	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	a.AddGold(req->add);
	const std::uint32_t combat_gold = a.GetGold();
	const std::string out_blob = a.SerializePersistentState();
	runtime().CacheCharacterState(world_code, char_id, out_blob);
	spdlog::debug("PlayerActor hot dirty marked. char_id={} flags={} version={}",
		char_id,
		static_cast<std::uint32_t>(a.MutableHotState().dirty_flags),
		a.MutableHotState().version);

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
	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("get_stats", sid, char_id)) {
		return true;
	}
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	if (const auto* hot = a.HotState()) {
		a.SyncProjectionFromCoreState();
	}

	proto::S2C_stats res{};
	res.char_id = char_id;
	res.hp = a.GetCurrentHp();
	res.max_hp = a.GetMaxHp();
	res.atk = a.GetAttack();
	res.def = a.GetDefense();
	res.gold = a.GetGold();

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
 	}
	
	if (const auto* core = a.CoreState()) {
		const auto status = svr::ItemTemplateRepository::SnapshotStatus();
		spdlog::debug(
			"World stats summary: char_id={} level={} job={} tribe={} weapon={} armor={} costume={} accessory={} source={} preload_count={} miss_count={} fallback_entered={} max_hp={} atk={} def={} gold={}",
			char_id,
			core->identity.level,
			core->identity.job,
			core->identity.tribe,
			core->equip.weapon_template_id,
			core->equip.armor_template_id,
			core->equip.costume_template_id,
			core->equip.accessory_template_id,
			status.source,
			status.preload_count,
			status.miss_count,
			status.fallback_entered,
			a.GetMaxHp(),
			a.GetAttack(),
			a.GetDefense(),
			a.GetGold());
	}
	return true;
}

bool WorldHandler::HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_heal_self>(body, body_len);
	if (!req) return false;

	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("heal_self", sid, char_id)) {
		return true;
	}
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	const auto max_hp = a.GetMaxHp();
	if (req->amount == 0) a.SetCurrentHp(max_hp);
	else {
		const std::uint64_t nhp =
			static_cast<std::uint64_t>(a.GetCurrentHp()) +
			static_cast<std::uint64_t>(req->amount);
		a.SetCurrentHp(static_cast<std::uint32_t>(std::min<std::uint64_t>(max_hp, nhp)));
	}

	proto::S2C_stats res{};
	res.char_id = char_id;
	res.hp = a.GetCurrentHp();
	res.max_hp = a.GetMaxHp();
	res.atk = a.GetAttack();
	res.def = a.GetDefense();
	res.gold = a.GetGold();

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
 	}
	return true;
}




