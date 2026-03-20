#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"
#include "proto/internal/world_zone_proto.h"

namespace pt_wz = proto::internal::world_zone;

class WorldZoneHandler : public dc::ServiceLineHandlerBase
{
public:
	using RegisterCallback = std::function<void(
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
		std::string_view server_name)>;

	using HeartbeatCallback = std::function<void(
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
		std::uint32_t flags)>;

	using UnregisterCallback = std::function<void(std::uint32_t sid, std::uint32_t serial)>;
	using MapAssignResponseCallback = std::function<void(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldMapAssignResponse&)>;
	using PlayerEnterAckCallback = std::function<void(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldPlayerEnterAck&)>;


	WorldZoneHandler(
		RegisterCallback on_register,
		HeartbeatCallback on_heartbeat,
		UnregisterCallback on_unregister,
		MapAssignResponseCallback on_map_assign_response,
		PlayerEnterAckCallback on_player_enter_ack);

	bool SendMapAssignRequest(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t request_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		bool create_if_missing,
		bool dungeon_instance);

	bool SendPlayerEnter(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t request_id,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::uint16_t zone_id);

	bool SendPlayerLeave(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::uint16_t zone_id);

protected:
	bool DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg) override;
	void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
	void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
	bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial) override;

private:
	bool SendRegisterAck(
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
		bool accepted);

	RegisterCallback on_register_;
	HeartbeatCallback on_heartbeat_;
	UnregisterCallback on_unregister_;
	MapAssignResponseCallback on_map_assign_response_;
	PlayerEnterAckCallback on_player_enter_ack_;
};
