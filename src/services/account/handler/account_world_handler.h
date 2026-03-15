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
        std::string_view server_name,
        std::string_view public_host,
        std::uint16_t public_port)>;

    using DisconnectCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial)>;

public:
    AccountWorldHandler(
        RegisterHelloCallback on_register_hello,
        DisconnectCallback on_disconnect);

    ~AccountWorldHandler() override = default;

public:
    bool SendRegisterAck(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint8_t accepted,
        std::uint32_t server_id,
        std::string_view server_name,
        std::string_view public_host,
        std::uint16_t public_port);

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
};
