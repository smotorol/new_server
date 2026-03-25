#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"
#include "services/world/runtime/i_world_runtime.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

class WorldAccountHandler : public dc::ServiceLineHandlerBase
{
public:
	using RegisterAckCallback = std::function<void(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t active_zone_count,
		std::uint16_t load_score,
		std::uint32_t flags,
		std::string_view server_name,
		std::string_view public_host,
		std::uint16_t public_port)>;

	using DisconnectCallback = std::function<void(
		std::uint32_t sid,
		std::uint32_t serial)>;

public:
	WorldAccountHandler(
		svr::IWorldRuntime& runtime,
		RegisterAckCallback on_register_ack,
		DisconnectCallback on_disconnect);

	~WorldAccountHandler() override = default;

public:
	void SetServerIdentity(
 		std::uint32_t server_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
 		std::string server_name,
 		std::string public_host,
		std::uint16_t public_port,
		std::uint16_t active_zone_count = 0,
		std::uint16_t load_score = 0,
		std::uint32_t flags = pt_aw::k_world_flag_accepting_players | pt_aw::k_world_flag_visible);

	bool SendHelloRegister(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial);

	bool SendRouteHeartbeat(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial);

	bool SendWorldAuthTicketConsumeRequest(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::string_view login_session,
		std::string_view world_token);

	bool SendWorldEnterSuccessNotify(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial,
		std::uint64_t trace_id,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token);

protected:
	bool DataAnalysis(
		std::uint32_t dwProID,
		std::uint32_t n,
		_MSG_HEADER* pMsgHeader,
		char* pMsg) override;

	void OnLineAccepted(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;

	void OnLineClosed(
		std::uint32_t dwProID,
		std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;

	bool ShouldHandleClose(
		std::uint32_t dwIndex,
		std::uint32_t dwSerial) override;

private:
	svr::IWorldRuntime& runtime_;
	std::uint32_t server_id_ = 0;
	std::uint16_t world_id_ = 0;
	std::uint16_t channel_id_ = 0;
	std::string server_name_;
	std::string public_host_;
	std::uint16_t public_port_ = 0;
	std::uint16_t active_zone_count_ = 0;
	std::uint16_t load_score_ = 0;
	std::uint32_t flags_ = pt_aw::k_world_flag_accepting_players | pt_aw::k_world_flag_visible;

	RegisterAckCallback on_register_ack_;
	DisconnectCallback on_disconnect_;
};
