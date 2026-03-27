#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "proto/internal/login_account_proto.h"
#include "server_common/handler/service_line_handler_base.h"

namespace pt_la = proto::internal::login_account;

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
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::string_view login_id,
        std::string_view password)>;

    using WorldListRequestCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::string_view login_session)>;

    using WorldSelectRequestCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::uint16_t channel_id,
        std::string_view login_session)>;

    using CharacterListRequestCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::string_view login_session)>;

    using CharacterSelectRequestCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session)>;

public:
    AccountLoginHandler(
        RegisterHelloCallback on_register_hello,
        DisconnectCallback on_disconnect,
        AuthRequestCallback on_auth_request,
        WorldListRequestCallback on_world_list_request,
        WorldSelectRequestCallback on_world_select_request,
        CharacterListRequestCallback on_character_list_request,
        CharacterSelectRequestCallback on_character_select_request);

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
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token,
        std::string_view world_host,
        std::string_view fail_reason,
        std::uint16_t world_port);

    bool SendWorldListResult(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint16_t count,
        const pt_la::WorldSummary* worlds,
        std::string_view fail_reason);

    bool SendWorldSelectResult(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::uint16_t channel_id,
        std::uint32_t world_server_id,
        std::string_view login_session,
        std::string_view world_host,
        std::string_view fail_reason,
        std::uint16_t world_port);

    bool SendCharacterListResult(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint16_t count,
        const pt_la::CharacterSummary* characters,
        std::string_view fail_reason);

    bool SendCharacterSelectResult(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        bool ok,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token,
        std::string_view world_host,
        std::string_view fail_reason,
        std::uint16_t world_port);
    
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
    RegisterHelloCallback on_register_hello_;
    DisconnectCallback on_disconnect_;
    AuthRequestCallback on_auth_request_;
    WorldListRequestCallback on_world_list_request_;
    WorldSelectRequestCallback on_world_select_request_;
    CharacterListRequestCallback on_character_list_request_;
    CharacterSelectRequestCallback on_character_select_request_;
};
