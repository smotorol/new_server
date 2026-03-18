#include "services/world/handler/world_zone_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"

WorldZoneHandler::WorldZoneHandler(RegisterCallback on_register, HeartbeatCallback on_heartbeat, UnregisterCallback on_unregister, MapAssignResponseCallback on_map_assign_response)
	: on_register_(std::move(on_register))
	, on_heartbeat_(std::move(on_heartbeat))
	, on_unregister_(std::move(on_unregister))
	, on_map_assign_response_(std::move(on_map_assign_response))
{
}

bool WorldZoneHandler::SendRegisterAck(
	std::uint32_t dwProID,
	std::uint32_t sid,
	std::uint32_t serial,
	std::uint32_t server_id,
	std::uint16_t zone_id,
	std::uint16_t world_id,
	std::uint16_t channel_id,
	std::uint16_t map_instance_capacity,
	std::uint16_t active_map_instance_count,
	std::uint16_t active_player_count,
	std::uint16_t load_score,
	std::uint32_t flags,
	std::string_view server_name,
	bool accepted)
{
	pt_wz::ZoneServerRegisterAck ack{};
	ack.accepted = accepted ? 1 : 0;
	ack.server_id = server_id;
	ack.zone_id = zone_id;
	ack.world_id = world_id;
	ack.channel_id = channel_id;
	ack.map_instance_capacity = map_instance_capacity;
	ack.active_map_instance_count = active_map_instance_count;
	ack.active_player_count = active_player_count;
	ack.load_score = load_score;
	ack.flags = flags;
	std::snprintf(ack.server_name, sizeof(ack.server_name), "%.*s", static_cast<int>(server_name.size()), server_name.data());

	const auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::zone_server_register_ack),
		static_cast<std::uint16_t>(sizeof(ack)));

	return Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&ack));
}

bool WorldZoneHandler::SendMapAssignRequest(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t request_id,
	std::uint32_t map_template_id,
	std::uint32_t instance_id,
	bool create_if_missing,
	bool dungeon_instance)
{
	pt_wz::WorldZoneMapAssignRequest pkt{};
	pkt.request_id = request_id;
	pkt.map_template_id = map_template_id;
	pkt.instance_id = instance_id;
	pkt.create_if_missing = create_if_missing ? 1 : 0;
	pkt.dungeon_instance = dungeon_instance ? 1 : 0;

	auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::world_zone_map_assign_request),
		static_cast<std::uint16_t>(sizeof(pkt)));

	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::SendPlayerEnter(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t char_id,
	std::uint32_t map_template_id,
	std::uint32_t instance_id,
	std::uint16_t zone_id)
{
	pt_wz::WorldZonePlayerEnter pkt{};
	pkt.char_id = char_id;
	pkt.map_template_id = map_template_id;
	pkt.instance_id = instance_id;
	pkt.zone_id = zone_id;
	auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::world_zone_player_enter),
		static_cast<std::uint16_t>(sizeof(pkt)));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::SendPlayerLeave(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t char_id,
	std::uint32_t map_template_id,
	std::uint32_t instance_id,
	std::uint16_t zone_id)
{
	pt_wz::WorldZonePlayerLeave pkt{};
	pkt.char_id = char_id;
	pkt.map_template_id = map_template_id;
	pkt.instance_id = instance_id;
	pkt.zone_id = zone_id;
	auto h = proto::make_header(
		static_cast<std::uint16_t>(pt_wz::WorldZoneMsg::world_zone_player_leave),
		static_cast<std::uint16_t>(sizeof(pkt)));
	return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	if (!pMsgHeader) {
		return false;
	}

	const auto msg_type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

	switch (static_cast<pt_wz::WorldZoneMsg>(msg_type)) {
	case pt_wz::WorldZoneMsg::zone_server_hello:
		{
			const auto* req = proto::as<pt_wz::ZoneServerHello>(pMsg, body_len);
			if (!req) {
				spdlog::error("WorldZoneHandler invalid zone_server_hello sid={}", n);
				return false;
			}

			const auto serial = GetLatestSerial(n);
			if (on_register_) {
				on_register_(n, serial, req->server_id, req->zone_id, req->world_id, req->channel_id,
					req->map_instance_capacity, req->active_map_instance_count, req->active_player_count, req->load_score, req->flags, req->server_name);
			}

			if (!SendRegisterAck(dwProID, n, serial, req->server_id, req->zone_id, req->world_id, req->channel_id,
				req->map_instance_capacity, req->active_map_instance_count, req->active_player_count, req->load_score, req->flags, req->server_name, true)) {
				spdlog::error("WorldZoneHandler failed to send register ack. sid={} serial={} zone_id={}", n, serial, req->zone_id);
			}
			return true;
		}
	case pt_wz::WorldZoneMsg::zone_server_route_heartbeat:
		{
			const auto* req = proto::as<pt_wz::ZoneServerRouteHeartbeat>(pMsg, body_len);
			if (!req) {
				spdlog::error("WorldZoneHandler invalid zone_server_route_heartbeat sid={}", n);
				return false;
			}

			if (on_heartbeat_) {
				on_heartbeat_(n, GetLatestSerial(n), req->server_id, req->zone_id, req->world_id, req->channel_id,
					req->map_instance_capacity, req->active_map_instance_count, req->active_player_count, req->load_score, req->flags);
			}
			return true;
		}
	case pt_wz::WorldZoneMsg::zone_world_map_assign_response:
		{
			const auto* res = proto::as<pt_wz::ZoneWorldMapAssignResponse>(pMsg, body_len);
			if (!res) {
				spdlog::error("WorldZoneHandler invalid zone_world_map_assign_response sid={}", n);
				return false;
			}
			if (on_map_assign_response_) {
				on_map_assign_response_(n, GetLatestSerial(n), *res);
			}
			return true;
		}
	default:
		spdlog::warn("WorldZoneHandler unknown msg_type={} sid={}", msg_type, n);
		return false;
	}
}

void WorldZoneHandler::OnLineAccepted(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("WorldZoneHandler::OnZoneAccepted index={} serial={}", dwIndex, dwSerial);
}

bool WorldZoneHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	return GetLatestSerial(dwIndex) == dwSerial;
}

void WorldZoneHandler::OnLineClosed(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (on_unregister_) {
		on_unregister_(dwIndex, dwSerial);
	}
	spdlog::info("WorldZoneHandler::OnZoneDisconnected index={} serial={}", dwIndex, dwSerial);
}
