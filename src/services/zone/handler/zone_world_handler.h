#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server_common/handler/service_line_handler_base.h"
#include "proto/internal/world_zone_proto.h"

namespace pt_wz = proto::internal::world_zone;

namespace svr { class ZoneRuntime; }

class ZoneWorldHandler : public dc::ServiceLineHandlerBase
{
public:
	ZoneWorldHandler(
		svr::ZoneRuntime& runtime,
		std::uint32_t server_id,
		std::uint16_t zone_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::string server_name);

	bool SendHelloRegister(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial);
	bool SendRouteHeartbeat(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial);

	bool SendMapAssignResponse(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint16_t result_code,
		std::uint16_t zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id);
	bool SendPlayerEnterAck(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint16_t result_code,
		std::uint16_t zone_id,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id);
	bool SendAoiSnapshot(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::uint16_t zone_id,
		std::uint16_t channel_id,
		std::int32_t self_x,
		std::int32_t self_y,
		const std::vector<proto::S2C_player_spawn_item>& items);
	bool SendAoiSpawnBatch(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::uint16_t zone_id,
		std::uint16_t channel_id,
		const std::vector<proto::S2C_player_spawn_item>& items);
	bool SendAoiDespawnBatch(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::uint16_t zone_id,
		std::uint16_t channel_id,
		const std::vector<proto::S2C_player_despawn_item>& items);
	bool SendAoiMoveBatch(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::uint16_t zone_id,
		std::uint16_t channel_id,
		const std::vector<proto::S2C_player_move_item>& items);
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
	svr::ZoneRuntime& runtime_;
};
