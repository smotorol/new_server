#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"

class AccountLoginHandler : public dc::ServiceLineHandlerBase
{
public:
    using RegisterHelloCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port)>;

    using DisconnectCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial)>;

    using AuthRequestCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t request_id,
        std::string_view login_id,
        std::string_view password,
        std::uint64_t selected_char_id)>;

public:
    AccountLoginHandler(
        RegisterHelloCallback on_register_hello,
        DisconnectCallback on_disconnect,
        AuthRequestCallback on_auth_request);

    ~AccountLoginHandler() override = default;

public:
    bool SendRegisterAck(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint8_t accepted,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port);

    bool SendAccountAuthResult(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint64_t char_id,
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
    RegisterHelloCallback on_register_hello_;
    DisconnectCallback on_disconnect_;
    AuthRequestCallback on_auth_request_;
};
