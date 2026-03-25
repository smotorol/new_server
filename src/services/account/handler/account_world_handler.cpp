#include "services/account/handler/account_world_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

AccountWorldHandler::AccountWorldHandler(
	RegisterHelloCallback on_register_hello,
	DisconnectCallback on_disconnect,
	ConsumeRequestCallback on_consume_request,
	EnterWorldSuccessCallback on_enter_world_success,
	RouteHeartbeatCallback on_route_heartbeat)
	: on_register_hello_(std::move(on_register_hello))
	, on_disconnect_(std::move(on_disconnect))
	, on_consume_request_(std::move(on_consume_request))
	, on_enter_world_success_(std::move(on_enter_world_success))
	, on_route_heartbeat_(std::move(on_route_heartbeat))
{
}

bool AccountWorldHandler::SendRegisterAck(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint8_t accepted,
	std::uint32_t server_id,
	std::uint16_t world_id,
	std::uint16_t channel_id,
	std::uint16_t active_zone_count,
	std::uint16_t load_score,
	std::uint32_t flags,
	std::string_view server_name,
	std::string_view public_host,
	std::uint16_t public_port)
{
	pt_aw::WorldServerRegisterAck pkt{};
	pkt.accepted = accepted;
	pkt.server_id = server_id;
	pkt.public_port = public_port;
	pkt.world_id = world_id;
	pkt.channel_id = channel_id;
	pkt.active_zone_count = active_zone_count;
	pkt.load_score = load_score;
	pkt.flags = flags;

	std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%.*s",
		static_cast<int>(server_name.size()), server_name.data());
	std::snprintf(pkt.public_host, sizeof(pkt.public_host), "%.*s",
		static_cast<int>(public_host.size()), public_host.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_server_register_ack),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountWorldHandler::SendWorldAuthTicketConsumeResponse(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	std::uint16_t result_code,
	std::uint64_t account_id,
	std::uint64_t char_id,
	std::string_view login_session,
	std::string_view world_token)
{
	pt_aw::WorldAuthTicketConsumeResponse pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.result_code = result_code;
	pkt.account_id = account_id;
	pkt.char_id = char_id;

	std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
		static_cast<int>(login_session.size()), login_session.data());
	std::snprintf(pkt.world_token, sizeof(pkt.world_token), "%.*s",
		static_cast<int>(world_token.size()), world_token.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_auth_ticket_consume_response),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountWorldHandler::DataAnalysis(
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
	case pt_aw::AccountWorldMsg::world_server_hello:
		{
			const auto* hello = proto::as<pt_aw::WorldServerHello>(pMsg, body_len);
			if (!hello) {
				spdlog::error("AccountWorldHandler invalid world_server_hello sid={}", n);
				return false;
			}

			if (on_register_hello_) {
				on_register_hello_(
					n,
					GetLatestSerial(n),
					hello->server_id,
					hello->world_id,
					hello->channel_id,
					hello->active_zone_count,
					hello->load_score,
					hello->flags,
					hello->server_name,
					hello->public_host,
					hello->public_port);
			}
			return true;
		}

	case pt_aw::AccountWorldMsg::world_server_route_heartbeat:
		{
			const auto* hb = proto::as<pt_aw::WorldServerRouteHeartbeat>(pMsg, body_len);
			if (!hb) {
				spdlog::error("AccountWorldHandler invalid world_server_route_heartbeat sid={}", n);
				return false;
			}

			if (on_route_heartbeat_) {
				on_route_heartbeat_(
					n,
					GetLatestSerial(n),
					hb->server_id,
					hb->world_id,
					hb->channel_id,
					hb->active_zone_count,
					hb->load_score,
					hb->flags);
			}
			return true;
		}

	case pt_aw::AccountWorldMsg::world_auth_ticket_consume_request:
		{
			const auto* req = proto::as<pt_aw::WorldAuthTicketConsumeRequest>(pMsg, body_len);
			if (!req) {
				spdlog::error("AccountWorldHandler invalid world_auth_ticket_consume_request sid={}", n);
				return false;
			}

			if (on_consume_request_) {
				on_consume_request_(
					n,
					GetLatestSerial(n),
					req->trace_id,
					req->request_id,
					req->account_id,
					req->login_session,
					req->world_token);
			}
			return true;
		}

	case pt_aw::AccountWorldMsg::world_enter_success_notify:
		{
			const auto* req = proto::as<pt_aw::WorldEnterSuccessNotify>(pMsg, body_len);
			if (!req) {
				spdlog::error("AccountWorldHandler invalid world_enter_success_notify sid={}", n);
				return false;
			}

			if (on_enter_world_success_) {
				on_enter_world_success_(
                    n,
                    GetLatestSerial(n),
					req->trace_id,
					req->account_id,
					req->char_id,
					req->login_session,
					req->world_token);
			}
			return true;
		}

	default:
		spdlog::warn("AccountWorldHandler unknown msg_type={} sid={}", msg_type, n);
		return false;
	}
}

void AccountWorldHandler::OnLineAccepted(
	std::uint32_t,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	spdlog::info("AccountWorldHandler accepted index={} serial={}", dwIndex, dwSerial);
}

bool AccountWorldHandler::ShouldHandleClose(
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	return GetLatestSerial(dwIndex) == dwSerial;
}

void AccountWorldHandler::OnLineClosed(
	std::uint32_t,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	if (on_disconnect_) {
		on_disconnect_(dwIndex, dwSerial);
	}
}
