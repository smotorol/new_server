#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "server_common/handler/service_line_handler_base.h"
#include "proto/internal/world_zone_proto.h"

namespace pt_wz = proto::internal::world_zone;

class ZoneWorldHandler : public dc::ServiceLineHandlerBase
{
public:
	using RegisterAckCallback = std::function<void(std::uint32_t sid, std::uint32_t serial)>;
	using DisconnectCallback = std::function<void(std::uint32_t sid, std::uint32_t serial)>;
	using MetricCallback = std::function<std::uint16_t()>;
	using FlagsCallback = std::function<std::uint32_t()>;
	using MapAssignRequestCallback = std::function<void(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZoneMapAssignRequest& req)>;
	using PlayerEnterRequestCallback = std::function<void(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerEnter& req)>;
	using PlayerLeaveRequestCallback = std::function<void(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerLeave& req)>;


	ZoneWorldHandler(
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
		PlayerLeaveRequestCallback on_player_leave);

	bool SendHelloRegister(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial);
	bool SendRouteHeartbeat(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial);

	bool SendMapAssignResponse(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t request_id,
		std::uint16_t result_code,
		std::uint16_t zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id);
protected:
	bool DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg) override;
	void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
	void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
	bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial) override;

private:
	std::uint32_t server_id_ = 0;
	std::uint16_t zone_id_ = 0;
	std::uint16_t world_id_ = 0;
	std::uint16_t channel_id_ = 0;
	std::string server_name_;
	RegisterAckCallback on_register_ack_;
	DisconnectCallback on_disconnect_;
	MetricCallback get_map_capacity_;
	MetricCallback get_active_map_count_;
	MetricCallback get_active_player_count_;
	MetricCallback get_load_score_;
	FlagsCallback get_flags_;
	MapAssignRequestCallback on_map_assign_request_;
	PlayerEnterRequestCallback on_player_enter_;
	PlayerLeaveRequestCallback on_player_leave_;
};
