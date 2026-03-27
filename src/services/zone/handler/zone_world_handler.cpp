#include "services/zone/handler/zone_world_handler.h"

#include <algorithm>
#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "services/zone/runtime/zone_runtime.h"

namespace pt_wz = proto::internal::world_zone;

ZoneWorldHandler::ZoneWorldHandler(
	svr::ZoneRuntime& runtime,
	std::uint32_t server_id,
	std::uint16_t zone_id,
	std::uint16_t world_id,
	std::uint16_t channel_id,
	std::string server_name)
	: server_id_(server_id), zone_id_(zone_id), world_id_(world_id), channel_id_(channel_id), server_name_(std::move(server_name)), runtime_(runtime)
{}

bool ZoneWorldHandler::SendHelloRegister(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	pt_wz::ZoneServerHello pkt{};
	pkt.server_id = server_id_; pkt.zone_id = zone_id_; pkt.world_id = world_id_; pkt.channel_id = channel_id_;
	pkt.map_instance_capacity = runtime_.GetMapInstanceCapacity();
	pkt.active_map_instance_count = runtime_.GetActiveMapInstanceCount();
	pkt.active_player_count = runtime_.GetActivePlayerCount();
	pkt.load_score = runtime_.GetLoadScore();
	pkt.flags = runtime_.GetFlagsFromHandler();
	std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%s", server_name_.c_str());
	const auto h = proto::make_header((std::uint16_t)pt_wz::WorldZoneMsg::zone_server_hello, (std::uint16_t)sizeof(pkt));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::SendRouteHeartbeat(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	pt_wz::ZoneServerRouteHeartbeat pkt{};
	pkt.server_id = server_id_; pkt.zone_id = zone_id_; pkt.world_id = world_id_; pkt.channel_id = channel_id_;
	pkt.map_instance_capacity = runtime_.GetMapInstanceCapacity();
	pkt.active_map_instance_count = runtime_.GetActiveMapInstanceCount();
	pkt.active_player_count = runtime_.GetActivePlayerCount();
	pkt.load_score = runtime_.GetLoadScore();
	pkt.flags = runtime_.GetFlagsFromHandler();
	const auto h = proto::make_header((std::uint16_t)pt_wz::WorldZoneMsg::zone_server_route_heartbeat, (std::uint16_t)sizeof(pkt));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::SendMapAssignResponse(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial, std::uint64_t trace_id, std::uint64_t request_id, std::uint16_t result_code, std::uint16_t zone_id, std::uint32_t map_template_id, std::uint32_t instance_id)
{
	pt_wz::ZoneWorldMapAssignResponse pkt{};
	pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.result_code = result_code; pkt.zone_id = zone_id; pkt.map_template_id = map_template_id; pkt.instance_id = instance_id;
	const auto h = proto::make_header((std::uint16_t)pt_wz::WorldZoneMsg::zone_world_map_assign_response, (std::uint16_t)sizeof(pkt));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::SendPlayerEnterAck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial, std::uint64_t trace_id, std::uint64_t request_id, std::uint16_t result_code, std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id)
{
	pt_wz::ZoneWorldPlayerEnterAck pkt{};
	pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.result_code = result_code; pkt.zone_id = zone_id; pkt.char_id = char_id; pkt.map_template_id = map_template_id; pkt.instance_id = instance_id;
	const auto h = proto::make_header((std::uint16_t)pt_wz::WorldZoneMsg::zone_world_player_enter_ack, (std::uint16_t)sizeof(pkt));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::DataAnalysis(std::uint32_t, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	if (!pMsgHeader) return false;
	const auto msg_type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;
	switch ((pt_wz::WorldZoneMsg)msg_type) {
	case pt_wz::WorldZoneMsg::zone_server_register_ack: {
		const auto* ack = proto::as<pt_wz::ZoneServerRegisterAck>(pMsg, body_len); if (!ack) return false;
		if (ack->accepted != 0) runtime_.OnWorldRegisterAckFromHandler(n, GetLatestSerial(n));
		return true; }
	case pt_wz::WorldZoneMsg::world_zone_map_assign_request: {
		const auto* req = proto::as<pt_wz::WorldZoneMapAssignRequest>(pMsg, body_len); if (!req) return false;
		runtime_.OnMapAssignRequestFromHandler(n, GetLatestSerial(n), *req); return true; }
	case pt_wz::WorldZoneMsg::world_zone_player_enter: {
		const auto* req = proto::as<pt_wz::WorldZonePlayerEnter>(pMsg, body_len); if (!req) return false;
		runtime_.OnPlayerEnterRequestFromHandler(n, GetLatestSerial(n), *req); return true; }
	case pt_wz::WorldZoneMsg::world_zone_player_leave: {
		const auto* req = proto::as<pt_wz::WorldZonePlayerLeave>(pMsg, body_len); if (!req) return false;
		runtime_.OnPlayerLeaveRequestFromHandler(n, GetLatestSerial(n), *req); return true; }
	default: spdlog::warn("ZoneWorldHandler unknown msg_type={} sid={}", msg_type, n); return false;
	}
}

void ZoneWorldHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
		spdlog::error("ZoneWorldHandler failed to send hello/register idx={} serial={}", dwIndex, dwSerial);
	}
}

void ZoneWorldHandler::OnLineClosed(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	runtime_.OnWorldDisconnectedFromHandler(dwIndex, dwSerial);
}

bool ZoneWorldHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	return GetLatestSerial(dwIndex) == dwSerial;
}
