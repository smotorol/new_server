#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

namespace svr { class WorldRuntime; }

class WorldAccountHandler : public dc::ServiceLineHandlerBase
{
public:
	explicit WorldAccountHandler(svr::WorldRuntime& runtime);

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

	bool SendWorldCharacterListResponse(
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
		std::string_view fail_reason);

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
	svr::WorldRuntime& runtime_;
	std::uint32_t server_id_ = 0;
	std::uint16_t world_id_ = 0;
	std::uint16_t channel_id_ = 0;
	std::string server_name_;
	std::string public_host_;
	std::uint16_t public_port_ = 0;
	std::uint16_t active_zone_count_ = 0;
	std::uint16_t load_score_ = 0;
	std::uint32_t flags_ = pt_aw::k_world_flag_accepting_players | pt_aw::k_world_flag_visible;

};
