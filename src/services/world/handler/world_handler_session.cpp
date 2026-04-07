#include "services/world/handler/world_handler.h"

#include <cstring>
#include <algorithm>
#include <limits>
#include <fmt/format.h>
#include <functional>
#include <memory>
#include <iostream>

#include <spdlog/spdlog.h>

#include "core/util/string_utils.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/common/protobuf_packet_codec.h"
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

#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
#include "proto/generated/cpp/client_world.pb.h"
#define DC_WORLD_CLIENT_PROTOBUF 1
#else
#define DC_WORLD_CLIENT_PROTOBUF 0
#endif

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

	void SendReconnectPlayerSpawnBatch_(
		WorldHandler& handler,
		std::uint32_t dwProID,
		std::uint32_t sid,
		std::uint32_t serial,
		const std::vector<proto::S2C_player_spawn_item>& spawn_items)
	{
#if DC_WORLD_CLIENT_PROTOBUF
		if (handler.IsSessionProtoMode(sid, serial)) {
			dc::proto::client::world::PlayerSpawnBatch msg;
			for (const auto& item : spawn_items) {
				auto* out = msg.add_items();
				out->set_char_id(item.char_id);
				out->set_x(item.x);
				out->set_y(item.y);
			}
			std::vector<char> framed;
			if (dc::proto::BuildFramedMessage((std::uint16_t)proto::S2CMsg::player_spawn_batch, msg, framed)) {
				_MSG_HEADER header{};
				std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
				handler.Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
				return;
			}
		}
#endif

		const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(spawn_items.size(), std::numeric_limits<std::uint16_t>::max()));
		const std::size_t body_size =
			sizeof(proto::S2C_player_spawn_batch) +
			(count > 0 ? (static_cast<std::size_t>(count) - 1) * sizeof(proto::S2C_player_spawn_item) : 0);
		std::vector<char> body(body_size);
		auto* pkt = reinterpret_cast<proto::S2C_player_spawn_batch*>(body.data());
		pkt->count = count;
		for (std::size_t i = 0; i < count; ++i) {
			pkt->items[i] = spawn_items[i];
		}
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn_batch, (std::uint16_t)body_size);
		handler.Send(dwProID, sid, serial, h, body.data());
	}

} // namespace

void WorldHandler::SendEnterWorldResult(
	std::uint32_t dwProID,
	std::uint32_t sid,
	std::uint32_t serial,
	bool ok,
	pt_w::EnterWorldResultCode reason,
	std::uint64_t account_id,
	std::uint64_t char_id,
	bool use_protobuf,
	std::string_view reconnect_token)
{
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::EnterWorldResult res;
		res.set_ok(ok);
		res.set_reason(static_cast<std::uint32_t>(reason));
		res.set_account_id(account_id);
		res.set_char_id(char_id);
		res.set_reconnect_token(std::string(reconnect_token));
		std::vector<char> framed;
		if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result), res, framed)) {
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
			return;
		}
	}
#endif

	pt_w::S2C_enter_world_result res{};
	res.ok = ok ? 1 : 0;
	res.reason = static_cast<std::uint16_t>(reason);
	res.account_id = account_id;
	res.char_id = char_id;
	std::snprintf(
		res.reconnect_token,
		static_cast<int>(sizeof(res.reconnect_token)),
		"%s",
		std::string(reconnect_token).c_str());
	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
		static_cast<std::uint16_t>(sizeof(res)));
	Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
}

void WorldHandler::SendStatsResponse(
	std::uint32_t dwProID,
	std::uint32_t sid,
	std::uint32_t serial,
	std::uint64_t char_id,
	std::uint32_t hp,
	std::uint32_t max_hp,
	std::uint32_t atk,
	std::uint32_t def,
	std::uint32_t gold,
	bool use_protobuf)
{
	proto::S2C_stats res{};
	res.char_id = char_id;
	res.hp = hp;
	res.max_hp = max_hp;
	res.atk = atk;
	res.def = def;
	res.gold = gold;

#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::StatsResponse msg;
		msg.set_char_id(char_id);
		msg.set_hp(hp);
		msg.set_max_hp(max_hp);
		msg.set_atk(atk);
		msg.set_def(def);
		msg.set_gold(gold);
		std::vector<char> framed;
		if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(proto::S2CMsg::stats), msg, framed)) {
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
			return;
		}
	}
#endif

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::stats, (std::uint16_t)sizeof(res));
	Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
}

void WorldHandler::SendLogoutWorldResult(
	std::uint32_t dwProID,
	std::uint32_t sid,
	std::uint32_t serial,
	bool ok,
	pt_w::LogoutType type,
	pt_w::LogoutWorldResultCode reason,
	std::uint64_t account_id,
	std::uint64_t char_id,
	bool use_protobuf)
{
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::LogoutWorldResult msg;
		msg.set_ok(ok);
		msg.set_type(static_cast<dc::proto::client::world::LogoutType>(type));
		msg.set_reason(static_cast<std::uint32_t>(reason));
		msg.set_account_id(account_id);
		msg.set_char_id(char_id);
		std::vector<char> framed;
		if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_w::WorldS2CMsg::logout_world_result), msg, framed)) {
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
			return;
		}
	}
#endif

	pt_w::S2C_logout_world_result res{};
	res.ok = ok ? 1 : 0;
	res.type = static_cast<std::uint16_t>(type);
	res.reason = static_cast<std::uint16_t>(reason);
	res.account_id = account_id;
	res.char_id = char_id;
	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_w::WorldS2CMsg::logout_world_result),
		static_cast<std::uint16_t>(sizeof(res)));
	Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
}

void WorldHandler::SendReconnectWorldResult(
	std::uint32_t dwProID,
	std::uint32_t sid,
	std::uint32_t serial,
	const svr::ReconnectWorldSessionResult& result,
	bool use_protobuf)
{
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::ReconnectWorldResult msg;
		msg.set_ok(result.ok());
		msg.set_reason(static_cast<std::uint32_t>(result.code));
		msg.set_account_id(result.current_session.account_id);
		msg.set_char_id(result.current_session.char_id);
		msg.set_reconnect_token(result.reconnect_token);
		msg.set_zone_id(result.zone_id);
		msg.set_map_id(result.map_id);
		msg.set_x(result.x);
		msg.set_y(result.y);
		std::vector<char> framed;
		if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_w::WorldS2CMsg::reconnect_world_result), msg, framed)) {
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
			return;
		}
	}
#endif

	pt_w::S2C_reconnect_world_result res{};
	res.ok = result.ok() ? 1 : 0;
	res.reason = static_cast<std::uint16_t>(result.code);
	res.account_id = result.current_session.account_id;
	res.char_id = result.current_session.char_id;
	res.zone_id = result.zone_id;
	res.map_id = result.map_id;
	res.x = result.x;
	res.y = result.y;
	std::snprintf(
		res.reconnect_token,
		static_cast<int>(sizeof(res.reconnect_token)),
		"%s",
		result.reconnect_token.c_str());
	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_w::WorldS2CMsg::reconnect_world_result),
		static_cast<std::uint16_t>(sizeof(res)));
	Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
}

bool WorldHandler::HandleEnterWorldWithToken(std::uint32_t dwProID, std::uint32_t n, const char* body, std::size_t body_len, bool use_protobuf)
{
	std::uint64_t account_id = 0;
	std::string login_session;
	std::string world_token;

#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::EnterWorldWithTokenRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		account_id = req.account_id();
		login_session = req.login_session();
		world_token = req.world_token();
	}
	else
#endif
	{
		auto* req = proto::as<pt_w::C2S_enter_world_with_token>(body, body_len);
		if (!req) return false;
		account_id = req->account_id;
		login_session = req->login_session;
		world_token = req->world_token;
	}

	const std::uint32_t serial = GetLatestSerial(n);
	SetSessionProtoMode(n, serial, use_protobuf);
	dc::enterlog::LogEnterFlow(
		spdlog::level::info,
		dc::enterlog::EnterStage::ClientWorldEnterRequestReceived,
		{ 0, account_id, 0, n, serial, login_session, world_token },
		{},
		use_protobuf ? "protobuf_enter_world_request" : "");

	if (serial == 0) {
		spdlog::warn(
			"HandleEnterWorldWithToken ignored because session serial is stale. sid={} account_id={}",
			n,
			account_id);
		return true;
	}

	if (!runtime().RequestConsumeWorldAuthTicket(
		n,
		serial,
		use_protobuf,
		0,
		account_id,
		login_session,
		world_token)) {
		SendEnterWorldResult(dwProID, n, serial, false, pt_w::EnterWorldResultCode::internal_error, account_id, 0, use_protobuf);
		dc::enterlog::LogEnterFlow(
			spdlog::level::warn,
			dc::enterlog::EnterStage::EnterFlowAborted,
			{ 0, account_id, 0, n, serial, login_session, world_token },
			"world_consume_request_send_failed");
		return true;
	}

	spdlog::info(
		"HandleEnterWorldWithToken consume requested. sid={} serial={} account_id={} login_session={} token={}",
		n,
		serial,
		account_id,
		login_session,
		world_token);

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

bool WorldHandler::ResolveAuthenticatedSessionOrReject_(
	const char* op_name,
	std::uint32_t sid,
	std::uint32_t serial,
	svr::WorldAuthedSession& out_session) const
{
	const auto current = runtime().TryGetAuthenticatedWorldSession(sid, serial);
	if (current.has_value()) {
		out_session = *current;
		return true;
	}

	svr::metrics::g_world_unauth_packet_rejects.fetch_add(1, std::memory_order_relaxed);
	svr::metrics::g_world_unauth_last_sid.store(sid, std::memory_order_relaxed);
	spdlog::warn(
		"[auth] rejected unauthenticated world packet. op={} sid={} serial={}",
		(op_name ? op_name : "unknown"),
		sid,
		serial);
	return false;
}

bool WorldHandler::HandleLogoutWorld(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len, bool use_protobuf)
{
	const auto serial = GetLatestSerial(sid);
	if (serial == 0) {
		return true;
	}

	pt_w::LogoutType logout_type = pt_w::LogoutType::soft;
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::LogoutWorldRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		logout_type = static_cast<pt_w::LogoutType>(req.type());
	}
	else
#endif
	{
		auto* req = proto::as<pt_w::C2S_logout_world>(body, body_len);
		if (!req) return false;
		logout_type = static_cast<pt_w::LogoutType>(req->type);
	}

	svr::WorldAuthedSession session{};
	if (!ResolveAuthenticatedSessionOrReject_("logout_world", sid, serial, session)) {
		SendLogoutWorldResult(
			dwProID,
			sid,
			serial,
			false,
			logout_type,
			pt_w::LogoutWorldResultCode::not_in_world,
			0,
			0,
			use_protobuf);
		return true;
	}

	const auto close_reason =
		logout_type == pt_w::LogoutType::hard
		? svr::WorldSessionCloseReason::ExplicitHardLogout
		: svr::WorldSessionCloseReason::ExplicitSoftLogout;
	runtime().MarkWorldSessionCloseReason(sid, serial, close_reason);
	SendLogoutWorldResult(
		dwProID,
		sid,
		serial,
		true,
		logout_type,
		pt_w::LogoutWorldResultCode::success,
		session.account_id,
		session.char_id,
		use_protobuf);

	spdlog::info(
		"HandleLogoutWorld explicit logout accepted. type={} account_id={} char_id={} sid={} serial={}",
		static_cast<int>(logout_type),
		session.account_id,
		session.char_id,
		sid,
		serial);

	Close(dwProID, sid, serial);
	return true;
}

bool WorldHandler::HandleReconnectWorld(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len, bool use_protobuf)
{
	const auto serial = GetLatestSerial(sid);
	if (serial == 0) {
		return true;
	}

	std::uint64_t account_id = 0;
	std::uint64_t char_id = 0;
	std::string reconnect_token;
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::ReconnectWorldRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		account_id = req.account_id();
		char_id = req.char_id();
		reconnect_token = req.reconnect_token();
	}
	else
#endif
	{
		auto* req = proto::as<pt_w::C2S_reconnect_world>(body, body_len);
		if (!req) return false;
		account_id = req->account_id;
		char_id = req->char_id;
		reconnect_token = req->reconnect_token;
	}

	const auto reconnect = runtime().TryReconnectWorldSession(
		account_id,
		char_id,
		reconnect_token,
		sid,
		serial);

	SendReconnectWorldResult(dwProID, sid, serial, reconnect, use_protobuf);
	if (!reconnect.ok()) {
		return true;
	}

	SetSessionProtoMode(sid, serial, use_protobuf || IsSessionProtoMode(sid, serial));

	proto::S2C_actor_bound bound{};
	bound.actor_id = reconnect.current_session.char_id;
	auto bh = proto::make_header(
		static_cast<std::uint16_t>(proto::S2CMsg::actor_bound),
		static_cast<std::uint16_t>(sizeof(bound)));
	Send(dwProID, sid, serial, bh, reinterpret_cast<const char*>(&bound));

	SendZoneMapState(
		dwProID,
		sid,
		serial,
		reconnect.current_session.char_id,
		reconnect.zone_id,
		reconnect.map_id,
		reconnect.x,
		reconnect.y,
		proto::ZoneMapStateReason::position_update);

	auto& actor = runtime().GetOrCreatePlayerActor(reconnect.current_session.char_id);
	SendStatsResponse(
		dwProID,
		sid,
		serial,
		reconnect.current_session.char_id,
		actor.GetCurrentHp(),
		actor.GetMaxHp(),
		actor.GetAttack(),
		actor.GetDefense(),
		actor.GetGold(),
		use_protobuf);

	runtime().PostActor(
		svr::MakeZoneActorId(static_cast<std::uint16_t>(reconnect.zone_id)),
		[this,
		 dwProID,
		 sid,
		 serial,
		 char_id = reconnect.current_session.char_id,
		 map_id = reconnect.map_id,
		 instance_id = reconnect.instance_id,
		 pos_x = reconnect.x,
		 pos_y = reconnect.y,
		 zone_id = static_cast<std::uint16_t>(reconnect.zone_id)]() {
			auto& z = runtime().GetOrCreateZoneActor(zone_id);
			const svr::Vec2i pos{ pos_x, pos_y };
			z.JoinOrUpdate(char_id, pos, sid, serial);
			std::vector<proto::S2C_player_spawn_item> spawn_items;
			for (const auto& [other_char_id, info] : z.players) {
				if (other_char_id == char_id) {
					continue;
				}
				proto::S2C_player_spawn_item item{};
				item.char_id = other_char_id;
				item.x = info.pos.x;
				item.y = info.pos.y;
				spawn_items.push_back(item);
			}
			const bool zone_snapshot_requested = runtime().RequestZoneInitialSnapshot(
				0,
				sid,
				serial,
				char_id,
				zone_id,
				static_cast<std::uint32_t>(map_id),
				instance_id,
				pos_x,
				pos_y,
				svr::ZoneSnapshotReason::reconnect);
			if (!zone_snapshot_requested) {
				spdlog::warn(
					"reconnect initial snapshot fallback disabled. sid={} serial={} char_id={} zone_id={} map_id={} instance_id={} would_have_spawn_batch_count={}",
					sid,
					serial,
					char_id,
					zone_id,
					map_id,
					instance_id,
					spawn_items.size());
			}
			spdlog::info(
				"reconnect initial snapshot prepared. sid={} serial={} char_id={} zone_id={} map_id={} instance_id={} spawn_batch_count={} zone_snapshot_requested={}",
				sid,
				serial,
				char_id,
				zone_id,
				map_id,
				instance_id,
				spawn_items.size(),
				zone_snapshot_requested ? 1 : 0);
		});

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
	ClearSessionProtoMode(dwIndex, dwSerial);

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

bool WorldHandler::HandleWorldAddGold(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len, bool use_protobuf)
{
	std::uint32_t add_amount = 0;
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::AddGoldRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		add_amount = req.add();
	}
	else
#endif
	{
		auto* req = proto::as<proto::C2S_add_gold>(body, body_len);
		if (!req) return false;
		add_amount = req->add;
	}

	const std::uint32_t world_code = 0;
	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("add_gold", sid, char_id)) {
		return true;
	}

	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	a.AddGold(add_amount);
	const std::uint32_t combat_gold = a.GetGold();
	const std::string out_blob = a.SerializePersistentState();
	runtime().CacheCharacterState(world_code, char_id, out_blob);
	spdlog::debug("PlayerActor hot dirty marked. char_id={} flags={} version={}",
		char_id,
		static_cast<std::uint32_t>(a.MutableHotState().dirty_flags),
		a.MutableHotState().version);

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		SetSessionProtoMode(sid, serial, use_protobuf || IsSessionProtoMode(sid, serial));
#if DC_WORLD_CLIENT_PROTOBUF
		if (IsSessionProtoMode(sid, serial)) {
			dc::proto::client::world::AddGoldResult msg;
			msg.set_ok(true);
			msg.set_gold(combat_gold);
			std::vector<char> framed;
			if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(proto::S2CMsg::add_gold_ok), msg, framed)) {
				_MSG_HEADER header{};
				std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
				Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
				return true;
			}
		}
#endif
		proto::S2C_add_gold_ok res{};
		res.ok = 1;
		res.gold = combat_gold;
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::add_gold_ok,
			(std::uint16_t)sizeof(res));
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}

bool WorldHandler::HandleWorldGetStats(std::uint32_t dwProID, std::uint32_t sid, bool use_protobuf)
{
	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("get_stats", sid, char_id)) {
		return true;
	}
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	if (const auto* hot = a.HotState()) {
		a.SyncProjectionFromCoreState();
	}

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		SetSessionProtoMode(sid, serial, use_protobuf || IsSessionProtoMode(sid, serial));
		SendStatsResponse(
			dwProID,
			sid,
			serial,
			char_id,
			a.GetCurrentHp(),
			a.GetMaxHp(),
			a.GetAttack(),
			a.GetDefense(),
			a.GetGold(),
			IsSessionProtoMode(sid, serial));
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

bool WorldHandler::HandleWorldHealSelf(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len, bool use_protobuf)
{
	std::uint32_t heal_amount = 0;
#if DC_WORLD_CLIENT_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::world::HealSelfRequest req;
		if (!req.ParseFromArray(body, static_cast<int>(body_len))) {
			return false;
		}
		heal_amount = req.amount();
	}
	else
#endif
	{
		auto* req = proto::as<proto::C2S_heal_self>(body, body_len);
		if (!req) return false;
		heal_amount = req->amount;
	}

	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("heal_self", sid, char_id)) {
		return true;
	}
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	const auto max_hp = a.GetMaxHp();
	if (heal_amount == 0) a.SetCurrentHp(max_hp);
	else {
		const std::uint64_t nhp =
			static_cast<std::uint64_t>(a.GetCurrentHp()) +
			static_cast<std::uint64_t>(heal_amount);
		a.SetCurrentHp(static_cast<std::uint32_t>(std::min<std::uint64_t>(max_hp, nhp)));
	}

	return HandleWorldGetStats(dwProID, sid, use_protobuf || IsSessionProtoMode(sid, GetLatestSerial(sid)));
}







