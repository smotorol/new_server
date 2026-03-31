#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "proto/internal/login_account_proto.h"
#include "server_common/handler/service_line_handler_base.h"

namespace pt_la = proto::internal::login_account;
namespace dc { class LoginLineRuntime; }

class LoginAccountHandler : public dc::ServiceLineHandlerBase
{
public:
    explicit LoginAccountHandler(dc::LoginLineRuntime& runtime);
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
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::string_view login_id,
        std::string_view password);

    bool SendWorldListRequest(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::string_view login_session);

    bool SendWorldSelectRequest(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::uint16_t channel_id,
        std::string_view login_session);

    bool SendCharacterListRequest(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::string_view login_session);

    bool SendCharacterSelectRequest(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session);

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
    dc::LoginLineRuntime& runtime_;
    std::uint32_t server_id_ = 0;
    std::string server_name_;
    std::uint16_t listen_port_ = 0;
};
