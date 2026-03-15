#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"

class WorldAccountHandler : public dc::ServiceLineHandlerBase
{
public:
    using RegisterAckCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::string_view public_host,
        std::uint16_t public_port)>;

    using DisconnectCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial)>;

public:
    WorldAccountHandler(
        RegisterAckCallback on_register_ack,
        DisconnectCallback on_disconnect);

    ~WorldAccountHandler() override = default;

public:
    void SetServerIdentity(
        std::uint32_t server_id,
        std::string server_name,
        std::string public_host,
        std::uint16_t public_port);

    bool SendHelloRegister(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial);

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
    std::uint32_t server_id_ = 0;
    std::string server_name_;
    std::string public_host_;
    std::uint16_t public_port_ = 0;

    RegisterAckCallback on_register_ack_;
    DisconnectCallback on_disconnect_;
};
