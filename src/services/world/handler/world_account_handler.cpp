#include "services/world/handler/world_account_handler.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

WorldAccountHandler::WorldAccountHandler(
	svr::IWorldRuntime& runtime,
	RegisterAckCallback on_register_ack,
	DisconnectCallback on_disconnect)
	: runtime_(runtime)
	, on_register_ack_(std::move(on_register_ack))
	, on_disconnect_(std::move(on_disconnect))
{
}

void WorldAccountHandler::SetServerIdentity(
	std::uint32_t server_id,
	std::uint16_t world_id,
	std::uint16_t channel_id,
	std::string server_name,
	std::string public_host,
	std::uint16_t public_port,
	std::uint16_t active_zone_count,
	std::uint16_t load_score,
	std::uint32_t flags)
{
	server_id_ = server_id;
	world_id_ = world_id;
	channel_id_ = channel_id;
	server_name_ = std::move(server_name);
	public_host_ = std::move(public_host);
	public_port_ = public_port;
	active_zone_count_ = active_zone_count;
	load_score_ = load_score;
	flags_ = flags;
}

bool WorldAccountHandler::SendHelloRegister(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	pt_aw::WorldServerHello pkt{};
	pkt.server_id = server_id_;
	pkt.public_port = public_port_;
	pkt.world_id = world_id_;
	pkt.channel_id = channel_id_;
	pkt.active_zone_count = active_zone_count_;
	pkt.load_score = load_score_;
	pkt.flags = flags_;

	std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%s", server_name_.c_str());
	std::snprintf(pkt.public_host, sizeof(pkt.public_host), "%s", public_host_.c_str());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_server_hello),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::SendRouteHeartbeat(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	pt_aw::WorldServerRouteHeartbeat pkt{};
	pkt.server_id = server_id_;
	pkt.world_id = world_id_;
	pkt.channel_id = channel_id_;
	pkt.active_zone_count = runtime_.GetActiveZoneCount();
	pkt.load_score = static_cast<std::uint16_t>(std::min<std::uint32_t>(runtime_.GetActiveWorldSessionCount(), 65535u));
	pkt.flags = flags_;

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_server_route_heartbeat),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::SendWorldAuthTicketConsumeRequest(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	std::uint64_t account_id,
	std::string_view login_session,
	std::string_view world_token)
{
	pt_aw::WorldAuthTicketConsumeRequest pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.account_id = account_id;
	std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
		static_cast<int>(login_session.size()), login_session.data());
	std::snprintf(pkt.world_token, sizeof(pkt.world_token), "%.*s",
		static_cast<int>(world_token.size()), world_token.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_auth_ticket_consume_request),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::SendWorldEnterSuccessNotify(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t account_id,
	std::uint64_t char_id,
	std::string_view login_session,
	std::string_view world_token)
{
	pt_aw::WorldEnterSuccessNotify pkt{};
	pkt.trace_id = trace_id;
	pkt.account_id = account_id;
	pkt.char_id = char_id;

	std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
		static_cast<int>(login_session.size()), login_session.data());
	std::snprintf(pkt.world_token, sizeof(pkt.world_token), "%.*s",
		static_cast<int>(world_token.size()), world_token.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_enter_success_notify),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::DataAnalysis(
	std::uint32_t dwProID,
	std::uint32_t n,
	_MSG_HEADER* pMsgHeader,
	char* pMsg)
{
	(void)dwProID;

	if (!pMsgHeader) {
		return false;
	}

	const auto msg_type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len =
		(pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

	switch (static_cast<pt_aw::AccountWorldMsg>(msg_type)) {
	case pt_aw::AccountWorldMsg::world_server_register_ack:
		{
			const auto* ack = proto::as<pt_aw::WorldServerRegisterAck>(pMsg, body_len);
			if (!ack) {
				spdlog::error("WorldAccountHandler invalid register_ack sid={}", n);
				return false;
			}

			if (ack->accepted == 0) {
				spdlog::warn("WorldAccountHandler register denied sid={} serial={}", n, GetLatestSerial(n));
				return true;
			}

			if (on_register_ack_) {
				on_register_ack_(
					n,
					GetLatestSerial(n),
					ack->server_id,
					ack->world_id,
					ack->channel_id,
					ack->active_zone_count,
					ack->load_score,
					ack->flags,
					ack->server_name,
					ack->public_host,
					ack->public_port);
			}
			return true;
		}
	case pt_aw::AccountWorldMsg::world_auth_ticket_consume_response:
		{
			const auto* req = proto::as<pt_aw::WorldAuthTicketConsumeResponse>(pMsg, body_len);
			if (!req) {
				spdlog::error("WorldAccountHandler invalid world_auth_ticket_consume_response sid={}", n);
				return false;
			}

			runtime_.OnWorldAuthTicketConsumeResponse(
				req->trace_id,
				req->request_id,
				static_cast<svr::ConsumePendingWorldAuthTicketResultKind>(req->result_code),
				req->account_id,
				req->char_id,
				req->login_session,
				req->world_token);
			return true;
		}
	default:
		spdlog::warn("WorldAccountHandler unknown msg_type={} sid={}", msg_type, n);
		return false;
	}
}

void WorldAccountHandler::OnLineAccepted(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
		spdlog::error("WorldAccountHandler failed to send hello/register idx={} serial={}", dwIndex, dwSerial);
	}
}

bool WorldAccountHandler::ShouldHandleClose(
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	return GetLatestSerial(dwIndex) == dwSerial;
}

void WorldAccountHandler::OnLineClosed(
	std::uint32_t,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	if (on_disconnect_) {
		on_disconnect_(dwIndex, dwSerial);
	}
}
