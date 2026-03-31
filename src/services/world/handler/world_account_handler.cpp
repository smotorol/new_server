#include "services/world/handler/world_account_handler.h"

#include <algorithm>
#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/account_world_proto.h"
#include "services/world/runtime/world_runtime.h"

namespace pt_aw = proto::internal::account_world;

WorldAccountHandler::WorldAccountHandler(svr::WorldRuntime& runtime)
	: runtime_(runtime)
{
}

void WorldAccountHandler::SetServerIdentity(
	std::uint32_t flags)
{
	flags_ = flags;
}

bool WorldAccountHandler::SendHelloRegister(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	pt_aw::WorldServerHello pkt{};
	pkt.public_port = runtime_.GetGatePort();

	auto name = runtime_.GetWorldName();
	auto host = runtime_.GetGateIP();
	std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%.*s",
		(int)name.size(), name.data());
	std::snprintf(pkt.public_host, sizeof(pkt.public_host), "%.*s",
		(int)host.size(), host.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::Msg::world_server_hello),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::SendRouteHeartbeat(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	pt_aw::WorldServerRouteHeartbeat pkt{};
	pkt.world_id = runtime_.GetWorldID();
	pkt.active_zone_count = runtime_.GetActiveZoneCount();
	pkt.load_score = static_cast<std::uint16_t>(std::min<std::uint32_t>(runtime_.GetActiveWorldSessionCount(), 65535u));
	pkt.flags = flags_;

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::Msg::world_server_route_heartbeat),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::SendReadyNotify(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial)
{
	pt_aw::WorldServerReadyNotify pkt{};
	pkt.world_id = runtime_.GetWorldID();
	pkt.flags = flags_;
	const auto h = proto::make_header(static_cast<std::uint16_t>(pt_aw::Msg::world_server_ready_notify), static_cast<std::uint16_t>(sizeof(pkt)));
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
		static_cast<std::uint16_t>(pt_aw::Msg::world_auth_ticket_consume_request),
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
		static_cast<std::uint16_t>(pt_aw::Msg::world_enter_success_notify),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}



bool WorldAccountHandler::SendWorldCharacterListResponse(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	bool ok,
	std::uint64_t account_id,
	std::uint16_t world_id,
	std::uint16_t count,
	std::string_view login_session,
	const pt_aw::WorldCharacterSummary* characters,
	std::string_view fail_reason)
{
	pt_aw::WorldCharacterListResponse pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.ok = ok ? 1 : 0;
	pkt.account_id = account_id;
	pkt.world_id = world_id;
	pkt.count = count;
	std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
		static_cast<int>(login_session.size()), login_session.data());
	std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s",
		static_cast<int>(fail_reason.size()), fail_reason.data());
	if (characters != nullptr) {
		for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_character_list_max_count); ++i) {
			pkt.characters[i] = characters[i];
		}
	}

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_aw::Msg::world_character_list_response),
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

	switch (static_cast<pt_aw::Msg>(msg_type)) {
	case pt_aw::Msg::world_server_register_ack:
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

			runtime_.OnAccountRegisterAckFromHandler(
				n,
				GetLatestSerial(n),
				ack->world_id,
				ack->db_dns,
				ack->db_id,
				ack->db_pw);
			return true;
		}
	case pt_aw::Msg::world_auth_ticket_consume_response:
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

	case pt_aw::Msg::world_character_list_request:
		{
			const auto* req = proto::as<pt_aw::WorldCharacterListRequest>(pMsg, body_len);
			if (!req) {
				spdlog::error("WorldAccountHandler invalid world_character_list_request sid={}", n);
				return false;
			}

			runtime_.OnAccountCharacterListRequest(
				n,
				GetLatestSerial(n),
				req->trace_id,
				req->request_id,
				req->account_id,
				req->world_id,
				req->login_session);
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
	runtime_.OnAccountDisconnectedFromHandler(dwIndex, dwSerial);
}
