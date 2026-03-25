#include "services/zone/handler/zone_world_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/world_zone_proto.h"

ZoneWorldHandler::ZoneWorldHandler(
	std::uint32_t server_id,
	std::uint16_t zone_id,
	std::uint16_t world_id,
	std::uint16_t channel_id,
	std::string server_name,
	RegisterAckCallback on_register_ack,
	DisconnectCallback on_disconnect,
	MetricCallback get_map_capacity,
	MetricCallback get_active_map_count,
	MetricCallback get_active_player_count,
	MetricCallback get_load_score,
	FlagsCallback get_flags,
	MapAssignRequestCallback on_map_assign_request,
	PlayerEnterRequestCallback on_player_enter,
	PlayerLeaveRequestCallback on_player_leave)
	: server_id_(server_id)
	, zone_id_(zone_id)
	, world_id_(world_id)
	, channel_id_(channel_id)
	, server_name_(std::move(server_name))
	, on_register_ack_(std::move(on_register_ack))
	, on_disconnect_(std::move(on_disconnect))
	, get_map_capacity_(std::move(get_map_capacity))
	, get_active_map_count_(std::move(get_active_map_count))
	, get_active_player_count_(std::move(get_active_player_count))
	, get_load_score_(std::move(get_load_score))
	, get_flags_(std::move(get_flags))
	, on_map_assign_request_(std::move(on_map_assign_request))
	, on_player_enter_(std::move(on_player_enter))
	, on_player_leave_(std::move(on_player_leave))
{
}

bool ZoneWorldHandler::SendHelloRegister(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	pt_wz::ZoneServerHello pkt{};
	pkt.server_id = server_id_;
	pkt.zone_id = zone_id_;
	pkt.world_id = world_id_;
	pkt.channel_id = channel_id_;
	pkt.map_instance_capacity = get_map_capacity_ ? get_map_capacity_() : 0;
	pkt.active_map_instance_count = get_active_map_count_ ? get_active_map_count_() : 0;
	pkt.active_player_count = get_active_player_count_ ? get_active_player_count_() : 0;
	pkt.load_score = get_load_score_ ? get_load_score_() : 0;
	pkt.flags = get_flags_ ? get_flags_() : 0;
	std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%s", server_name_.c_str());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::zone_server_hello),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::SendRouteHeartbeat(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	pt_wz::ZoneServerRouteHeartbeat pkt{};
	pkt.server_id = server_id_;
	pkt.zone_id = zone_id_;
	pkt.world_id = world_id_;
	pkt.channel_id = channel_id_;
	pkt.map_instance_capacity = get_map_capacity_ ? get_map_capacity_() : 0;
	pkt.active_map_instance_count = get_active_map_count_ ? get_active_map_count_() : 0;
	pkt.active_player_count = get_active_player_count_ ? get_active_player_count_() : 0;
	pkt.load_score = get_load_score_ ? get_load_score_() : 0;
	pkt.flags = get_flags_ ? get_flags_() : 0;

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::zone_server_route_heartbeat),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::SendMapAssignResponse(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	std::uint16_t result_code,
	std::uint16_t zone_id,
	std::uint32_t map_template_id,
	std::uint32_t instance_id)
{
	pt_wz::ZoneWorldMapAssignResponse pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.result_code = result_code;
	pkt.zone_id = zone_id;
	pkt.map_template_id = map_template_id;
	pkt.instance_id = instance_id;
	auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::zone_world_map_assign_response),
		static_cast<std::uint16_t>(sizeof(pkt)));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::SendPlayerEnterAck(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	std::uint16_t result_code,
	std::uint16_t zone_id,
	std::uint64_t char_id,
	std::uint32_t map_template_id,
	std::uint32_t instance_id)
{
	pt_wz::ZoneWorldPlayerEnterAck pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.result_code = result_code;
	pkt.zone_id = zone_id;
	pkt.char_id = char_id;
	pkt.map_template_id = map_template_id;
	pkt.instance_id = instance_id;
	auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::zone_world_player_enter_ack),
		static_cast<std::uint16_t>(sizeof(pkt)));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ZoneWorldHandler::DataAnalysis(std::uint32_t, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	if (!pMsgHeader) {
		return false;
	}

	const auto msg_type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len =
		(pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

	switch (static_cast<pt_wz::WorldZoneMsg>(msg_type)) {
	case pt_wz::WorldZoneMsg::zone_server_register_ack:
		{
			const auto* ack = proto::as<pt_wz::ZoneServerRegisterAck>(pMsg, body_len);
			if (!ack) {
				spdlog::error("ZoneWorldHandler invalid zone_server_register_ack sid={}", n);
				return false;
			}

			if (ack->accepted != 0 && on_register_ack_) {
				on_register_ack_(n, GetLatestSerial(n));
			}
			return true;
		}
	case pt_wz::WorldZoneMsg::world_zone_map_assign_request:
		{
			const auto* req = proto::as<pt_wz::WorldZoneMapAssignRequest>(pMsg, body_len);
			if (!req) {
				spdlog::error("ZoneWorldHandler invalid world_zone_map_assign_request sid={}", n);
				return false;
			}

			if (on_map_assign_request_) {
				on_map_assign_request_(n, GetLatestSerial(n), *req);
			}
			return true;
		}
	case pt_wz::WorldZoneMsg::world_zone_player_enter:
		{
			const auto* req = proto::as<pt_wz::WorldZonePlayerEnter>(pMsg, body_len);
			if (!req) {
				spdlog::error("ZoneWorldHandler invalid world_zone_player_enter sid={}", n);
				return false;
			}
			if (on_player_enter_) {
				on_player_enter_(n, GetLatestSerial(n), *req);
			}
			return true;
		}
	case pt_wz::WorldZoneMsg::world_zone_player_leave:
		{
			const auto* req = proto::as<pt_wz::WorldZonePlayerLeave>(pMsg, body_len);
			if (!req) {
				spdlog::error("ZoneWorldHandler invalid world_zone_player_leave sid={}", n);
				return false;
			}
			if (on_player_leave_) {
				on_player_leave_(n, GetLatestSerial(n), *req);
			}
			return true;
		}
	default:
		spdlog::warn("ZoneWorldHandler unknown msg_type={} sid={}", msg_type, n);
		return false;
	}
}

void ZoneWorldHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
		spdlog::error("ZoneWorldHandler failed to send hello/register idx={} serial={}", dwIndex, dwSerial);
	}
}

bool ZoneWorldHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	return GetLatestSerial(dwIndex) == dwSerial;
}

void ZoneWorldHandler::OnLineClosed(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (on_disconnect_) {
		on_disconnect_(dwIndex, dwSerial);
	}
}
