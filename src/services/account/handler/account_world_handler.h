#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"

class AccountWorldHandler : public dc::ServiceLineHandlerBase
{
public:
    using RegisterHelloCallback = std::function<void(
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

    using ConsumeRequestCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token)>;

    using EnterWorldSuccessCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token)>;

	using RouteHeartbeatCallback = std::function<void(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t active_zone_count,
		std::uint16_t load_score,
		std::uint32_t flags)>;
public:
    AccountWorldHandler(
        RegisterHelloCallback on_register_hello,
        DisconnectCallback on_disconnect,
        ConsumeRequestCallback on_consume_request,
        EnterWorldSuccessCallback on_enter_world_success,
        RouteHeartbeatCallback on_route_heartbeat);

    ~AccountWorldHandler() override = default;

public:
    bool SendRegisterAck(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint8_t accepted,
        std::uint32_t server_id,
        std::uint16_t world_id,
        std::uint16_t channel_id,
        std::uint16_t active_zone_count,
        std::uint16_t load_score,
        std::uint32_t flags,
        std::string_view server_name,
        std::string_view public_host,
        std::uint16_t public_port);

    bool SendWorldAuthTicketConsumeResponse(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t request_id,
        std::uint16_t result_code,
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
    RegisterHelloCallback on_register_hello_;
    DisconnectCallback on_disconnect_;
    ConsumeRequestCallback on_consume_request_;
    EnterWorldSuccessCallback on_enter_world_success_;
    RouteHeartbeatCallback on_route_heartbeat_;
};
