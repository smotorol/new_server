#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "proto/internal/login_account_proto.h"
#include "server_common/handler/service_line_handler_base.h"

namespace pt_la = proto::internal::login_account;

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
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token,
        std::string_view world_host,
        std::uint16_t world_port,
        std::string_view fail_reason)>;

    using WorldListResultCallback = std::function<void(
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint16_t count,
        const pt_la::WorldSummary* worlds,
        std::string_view fail_reason)>;

    using WorldSelectResultCallback = std::function<void(
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::uint16_t channel_id,
        std::uint32_t world_server_id,
        std::string_view login_session,
        std::string_view world_host,
        std::uint16_t world_port,
        std::string_view fail_reason)>;

    using CharacterListResultCallback = std::function<void(
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint16_t count,
        const pt_la::CharacterSummary* characters,
        std::string_view fail_reason)>;

    using CharacterSelectResultCallback = std::function<void(
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token,
        std::string_view world_host,
        std::uint16_t world_port,
        std::string_view fail_reason)>;

    using EnterWorldSuccessCallback = std::function<void(
        std::uint64_t trace_id,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token)>;
public:
    LoginAccountHandler(
        RegisterAckCallback on_register_ack,
        DisconnectCallback on_disconnect,
        AuthResultCallback on_auth_result,
        WorldListResultCallback on_world_list_result,
        WorldSelectResultCallback on_world_select_result,
        CharacterListResultCallback on_character_list_result,
        CharacterSelectResultCallback on_character_select_result,
        EnterWorldSuccessCallback on_enter_world_success);

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
    std::uint32_t server_id_ = 0;
    std::string server_name_;
    std::uint16_t listen_port_ = 0;

    RegisterAckCallback on_register_ack_;
    DisconnectCallback on_disconnect_;
    AuthResultCallback on_auth_result_;
    WorldListResultCallback on_world_list_result_;
    WorldSelectResultCallback on_world_select_result_;
    CharacterListResultCallback on_character_list_result_;
    CharacterSelectResultCallback on_character_select_result_;
    EnterWorldSuccessCallback on_enter_world_success_;
};
