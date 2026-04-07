#include "services/world/handler/world_zone_handler.h"

#include <cstring>
#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "services/world/runtime/world_runtime.h"

namespace {
	template <typename TPacket, typename TItem>
	bool ParseVariableBatchBody_(
		const char* body,
		std::size_t body_len,
		const TPacket*& out_packet,
		const TItem*& out_items,
		std::size_t& out_count)
	{
		const auto header_size = sizeof(TPacket) - sizeof(TItem);
		if (!body || body_len < header_size) {
			return false;
		}

		const auto* packet = reinterpret_cast<const TPacket*>(body);
		const auto count = static_cast<std::size_t>(packet->count);
		const auto required = header_size + count * sizeof(TItem);
		if (body_len < required) {
			return false;
		}

		out_packet = packet;
		out_count = count;
		out_items = count > 0 ? packet->items : nullptr;
		return true;
	}
}

WorldZoneHandler::WorldZoneHandler(svr::WorldRuntime& runtime)
    : runtime_(runtime)
{
}

bool WorldZoneHandler::SendRegisterAck(std::uint32_t dwProID, std::uint32_t sid, std::uint32_t serial,
    std::uint32_t server_id, std::uint16_t zone_id, std::uint16_t world_id, std::uint16_t channel_id,
    std::uint16_t map_instance_capacity, std::uint16_t active_map_instance_count, std::uint16_t active_player_count,
    std::uint16_t load_score, std::uint32_t flags, std::string_view server_name, bool accepted)
{
    pt_wz::ZoneServerRegisterAck ack{};
    ack.accepted = accepted ? 1 : 0; ack.server_id = server_id; ack.zone_id = zone_id; ack.world_id = world_id; ack.channel_id = channel_id;
    ack.map_instance_capacity = map_instance_capacity; ack.active_map_instance_count = active_map_instance_count; ack.active_player_count = active_player_count; ack.load_score = load_score; ack.flags = flags;
    std::snprintf(ack.server_name, sizeof(ack.server_name), "%.*s", (int)server_name.size(), server_name.data());
    const auto h = proto::make_header((std::uint16_t)pt_wz::Msg::zone_server_register_ack, (std::uint16_t)sizeof(ack));
    return Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&ack));
}

bool WorldZoneHandler::SendMapAssignRequest(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, std::uint32_t map_template_id, std::uint32_t instance_id,
    bool create_if_missing, bool dungeon_instance)
{
    pt_wz::WorldZoneMapAssignRequest pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.map_template_id = map_template_id; pkt.instance_id = instance_id;
    pkt.create_if_missing = create_if_missing ? 1 : 0; pkt.dungeon_instance = dungeon_instance ? 1 : 0;
    auto h = proto::make_header((std::uint16_t)pt_wz::Msg::world_zone_map_assign_request, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::SendPlayerEnter(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t char_id, std::uint32_t map_template_id,
    std::uint32_t instance_id, std::uint16_t zone_id)
{
    pt_wz::WorldZonePlayerEnter pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.char_id = char_id; pkt.map_template_id = map_template_id; pkt.instance_id = instance_id; pkt.zone_id = zone_id;
    auto h = proto::make_header((std::uint16_t)pt_wz::Msg::world_zone_player_enter, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::SendPlayerLeave(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id, std::uint16_t zone_id)
{
    pt_wz::WorldZonePlayerLeave pkt{};
    pkt.char_id = char_id; pkt.map_template_id = map_template_id; pkt.instance_id = instance_id; pkt.zone_id = zone_id;
    auto h = proto::make_header((std::uint16_t)pt_wz::Msg::world_zone_player_leave, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::SendPlayerMoveInternal(
	std::uint32_t dwProID,
	std::uint32_t dwIndex,
	std::uint32_t dwSerial,
	std::uint64_t trace_id,
	std::uint64_t request_id,
	std::uint64_t char_id,
	std::uint32_t sid,
	std::uint32_t serial,
	std::uint32_t map_template_id,
	std::uint32_t instance_id,
	std::uint16_t zone_id,
	std::uint16_t channel_id,
	std::int32_t x,
	std::int32_t y)
{
	pt_wz::WorldZonePlayerMoveInternal pkt{};
	pkt.trace_id = trace_id;
	pkt.request_id = request_id;
	pkt.char_id = char_id;
	pkt.sid = sid;
	pkt.serial = serial;
	pkt.map_template_id = map_template_id;
	pkt.instance_id = instance_id;
	pkt.zone_id = zone_id;
	pkt.channel_id = channel_id;
	pkt.x = x;
	pkt.y = y;
	auto h = proto::make_header((std::uint16_t)pt_wz::Msg::world_zone_player_move_internal, (std::uint16_t)sizeof(pkt));
	return TrySendLossy(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldZoneHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
    if (!pMsgHeader) return false;
    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len = (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;
    switch ((pt_wz::Msg)msg_type) {
    case pt_wz::Msg::zone_server_hello: {
        const auto* req = proto::as<pt_wz::ZoneServerHello>(pMsg, body_len); if (!req) return false;
        const auto serial = GetLatestSerial(n);
        runtime_.OnZoneRegisteredFromHandler(n, serial, req->server_id, req->zone_id, req->world_id, req->channel_id, req->map_instance_capacity, req->active_map_instance_count, req->active_player_count, req->load_score, req->flags, req->server_name);
        return SendRegisterAck(dwProID, n, serial, req->server_id, req->zone_id, req->world_id, req->channel_id, req->map_instance_capacity, req->active_map_instance_count, req->active_player_count, req->load_score, req->flags, req->server_name, true);
    }
    case pt_wz::Msg::zone_server_route_heartbeat: {
        const auto* req = proto::as<pt_wz::ZoneServerRouteHeartbeat>(pMsg, body_len); if (!req) return false;
        runtime_.OnZoneHeartbeatFromHandler(n, GetLatestSerial(n), req->server_id, req->zone_id, req->world_id, req->channel_id, req->map_instance_capacity, req->active_map_instance_count, req->active_player_count, req->load_score, req->flags); return true;
    }
    case pt_wz::Msg::zone_world_map_assign_response: {
        const auto* res = proto::as<pt_wz::ZoneWorldMapAssignResponse>(pMsg, body_len); if (!res) return false;
        runtime_.OnZoneMapAssignResponseFromHandler(n, GetLatestSerial(n), *res); return true;
    }
    case pt_wz::Msg::zone_world_player_enter_ack: {
        const auto* ack = proto::as<pt_wz::ZoneWorldPlayerEnterAck>(pMsg, body_len); if (!ack) return false;
        runtime_.OnZonePlayerEnterAckFromHandler(n, GetLatestSerial(n), *ack); return true;
    }
    case pt_wz::Msg::zone_world_aoi_snapshot: {
        const pt_wz::ZoneWorldAoiSnapshot* snapshot = nullptr;
        const proto::S2C_player_spawn_item* items = nullptr;
        std::size_t count = 0;
        if (!ParseVariableBatchBody_(pMsg, body_len, snapshot, items, count)) return false;
        spdlog::info("WorldZoneHandler AOI snapshot received. zone_sid={} zone_serial={} char_id={} map={} channel={} instance={} count={}",
            n, GetLatestSerial(n), snapshot->char_id, snapshot->map_template_id, snapshot->channel_id, snapshot->instance_id, count);
        runtime_.OnZoneAoiSnapshotFromHandler(n, GetLatestSerial(n), *snapshot, items, count);
        return true;
    }
    case pt_wz::Msg::zone_world_aoi_spawn_batch: {
        const pt_wz::ZoneWorldAoiSpawnBatch* batch = nullptr;
        const proto::S2C_player_spawn_item* items = nullptr;
        std::size_t count = 0;
        if (!ParseVariableBatchBody_(pMsg, body_len, batch, items, count)) return false;
        spdlog::info("WorldZoneHandler AOI spawn batch received. zone_sid={} zone_serial={} char_id={} map={} channel={} instance={} count={}",
            n, GetLatestSerial(n), batch->char_id, batch->map_template_id, batch->channel_id, batch->instance_id, count);
        runtime_.OnZoneAoiSpawnBatchFromHandler(n, GetLatestSerial(n), *batch, items, count);
        return true;
    }
    case pt_wz::Msg::zone_world_aoi_despawn_batch: {
        const pt_wz::ZoneWorldAoiDespawnBatch* batch = nullptr;
        const proto::S2C_player_despawn_item* items = nullptr;
        std::size_t count = 0;
        if (!ParseVariableBatchBody_(pMsg, body_len, batch, items, count)) return false;
        spdlog::info("WorldZoneHandler AOI despawn batch received. zone_sid={} zone_serial={} char_id={} map={} channel={} instance={} count={}",
            n, GetLatestSerial(n), batch->char_id, batch->map_template_id, batch->channel_id, batch->instance_id, count);
        runtime_.OnZoneAoiDespawnBatchFromHandler(n, GetLatestSerial(n), *batch, items, count);
        return true;
    }
    case pt_wz::Msg::zone_world_aoi_move_batch: {
        const pt_wz::ZoneWorldAoiMoveBatch* batch = nullptr;
        const proto::S2C_player_move_item* items = nullptr;
        std::size_t count = 0;
        if (!ParseVariableBatchBody_(pMsg, body_len, batch, items, count)) return false;
        spdlog::info("WorldZoneHandler AOI move batch received. zone_sid={} zone_serial={} char_id={} map={} channel={} instance={} count={}",
            n, GetLatestSerial(n), batch->char_id, batch->map_template_id, batch->channel_id, batch->instance_id, count);
        runtime_.OnZoneAoiMoveBatchFromHandler(n, GetLatestSerial(n), *batch, items, count);
        return true;
    }
    default: spdlog::warn("WorldZoneHandler unknown msg_type={} sid={}", msg_type, n); return false;
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
    runtime_.OnZoneDisconnectedFromHandler(dwIndex, dwSerial);
    spdlog::info("WorldZoneHandler::OnZoneDisconnected index={} serial={}", dwIndex, dwSerial);
}
