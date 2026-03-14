#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"

class LoginAccountHandler : public dc::ServiceLineHandlerBase
{
public:
    using RegisterAckCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port)>;

    using DisconnectCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial)>;

    using AuthResultCallback = std::function<void(
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view fail_reason)>;

public:
    LoginAccountHandler(
        RegisterAckCallback on_register_ack,
        DisconnectCallback on_disconnect,
        AuthResultCallback on_auth_result);

    ~LoginAccountHandler() override = default;

public:
    bool SendHelloRegister(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial);

    bool SendAccountAuthRequest(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t request_id,
        std::string_view login_id,
        std::string_view password,
        std::uint64_t selected_char_id);

    void SetServerIdentity(std::uint32_t server_id, std::string server_name, std::uint16_t listen_port);

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
    std::uint16_t listen_port_ = 0;

    RegisterAckCallback on_register_ack_;
    DisconnectCallback on_disconnect_;
    AuthResultCallback on_auth_result_;
};
