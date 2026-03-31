#include "services/account/handler/account_world_handler.h"

#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/account_world_proto.h"
#include "services/account/runtime/account_line_runtime.h"

namespace pt_aw = proto::internal::account_world;

AccountWorldHandler::AccountWorldHandler(dc::AccountLineRuntime& runtime)
	: runtime_(runtime)
{
}

bool AccountWorldHandler::SendRegisterAck(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint8_t accepted,
	std::uint16_t world_id,
	std::string_view db_dns,
	std::string_view db_id,
	std::string_view db_pw)
{
	pt_aw::WorldServerRegisterAck pkt{};
	pkt.accepted = accepted;
	pkt.world_id = world_id;

	std::snprintf(pkt.db_dns, sizeof(pkt.db_dns), "%.*s",
		static_cast<int>(db_dns.size()), db_dns.data());
	std::snprintf(pkt.db_id, sizeof(pkt.db_id), "%.*s",
		static_cast<int>(db_id.size()), db_id.data());
	std::snprintf(pkt.db_pw, sizeof(pkt.db_pw), "%.*s",
		static_cast<int>(db_pw.size()), db_pw.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::Msg::world_server_register_ack),
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
		static_cast<std::uint16_t>(pt_aw::Msg::world_auth_ticket_consume_response),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}



bool AccountWorldHandler::SendWorldCharacterListRequest(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	std::uint64_t account_id,
	std::uint16_t world_id,
	std::string_view login_session)
{
	pt_aw::WorldCharacterListRequest pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.account_id = account_id;
	pkt.world_id = world_id;
	std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
		static_cast<int>(login_session.size()), login_session.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::Msg::world_character_list_request),
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

	switch (static_cast<pt_aw::Msg>(msg_type)) {
	case pt_aw::Msg::world_server_hello:
		{
			const auto* hello = proto::as<pt_aw::WorldServerHello>(pMsg, body_len);
			if (!hello) {
				spdlog::error("AccountWorldHandler invalid world_server_hello sid={}", n);
				return false;
			}

			runtime_.On_world_server_hello(
				n,
				GetLatestSerial(n),
				hello->server_name,
				hello->public_host,
				hello->public_port);
			return true;
		}

	case pt_aw::Msg::world_server_ready_notify:
		{
			const auto* ready = proto::as<pt_aw::WorldServerReadyNotify>(pMsg, body_len);
			if (!ready) {
				spdlog::error("AccountWorldHandler invalid world_server_ready_notify sid={}", n);
				return false;
			}

			runtime_.OnWorldReadyNotifyFromHandler(
				n,
				GetLatestSerial(n),
				ready->world_id,
				ready->flags);
			return true;
		}

	case pt_aw::Msg::world_server_route_heartbeat:
		{
			const auto* hb = proto::as<pt_aw::WorldServerRouteHeartbeat>(pMsg, body_len);
			if (!hb) {
				spdlog::error("AccountWorldHandler invalid world_server_route_heartbeat sid={}", n);
				return false;
			}

			runtime_.OnWorldRouteHeartbeatReceived(
				n,
				GetLatestSerial(n),
				hb->world_id,
				hb->active_zone_count,
				hb->load_score,
				hb->flags);
			return true;
		}

	case pt_aw::Msg::world_auth_ticket_consume_request:
		{
			const auto* req = proto::as<pt_aw::WorldAuthTicketConsumeRequest>(pMsg, body_len);
			if (!req) {
				spdlog::error("AccountWorldHandler invalid world_auth_ticket_consume_request sid={}", n);
				return false;
			}

			runtime_.OnWorldTicketConsumeRequestFromHandler(
				n,
				GetLatestSerial(n),
				req->trace_id,
				req->request_id,
				req->account_id,
				req->login_session,
				req->world_token);
			return true;
		}

	case pt_aw::Msg::world_enter_success_notify:
		{
			const auto* req = proto::as<pt_aw::WorldEnterSuccessNotify>(pMsg, body_len);
			if (!req) {
				spdlog::error("AccountWorldHandler invalid world_enter_success_notify sid={}", n);
				return false;
			}

			runtime_.OnWorldEnterSuccessNotifyFromHandler(
				n,
				GetLatestSerial(n),
				req->trace_id,
				req->account_id,
				req->char_id,
				req->login_session,
				req->world_token);

			return true;
		}

	case pt_aw::Msg::world_character_list_response:
		{
			const auto* req = proto::as<pt_aw::WorldCharacterListResponse>(pMsg, body_len);
			if (!req) {
				spdlog::error("AccountWorldHandler invalid world_character_list_response sid={}", n);
				return false;
			}

			runtime_.OnWorldCharacterListResponseFromHandler(
				n,
				GetLatestSerial(n),
				req->trace_id,
				req->request_id,
				req->account_id,
				req->world_id,
				req->count,
				req->ok != 0,
				req->login_session,
				req->characters,
				req->fail_reason);

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
	runtime_.OnWorldRouteDisconnectedFromHandler(dwIndex, dwSerial);
}
