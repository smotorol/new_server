#pragma once

#include <cstdint>
#include <string_view>

#include "proto/internal/login_account_proto.h"
#include "server_common/handler/service_line_handler_base.h"

namespace pt_la = proto::internal::login_account;
namespace dc { class AccountLineRuntime; }

class AccountLoginHandler : public dc::ServiceLineHandlerBase
{
public:
    explicit AccountLoginHandler(dc::AccountLineRuntime& runtime);
    ~AccountLoginHandler() override = default;

    bool SendRegisterAck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint8_t accepted, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port);
    bool SendAccountAuthResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
        std::uint64_t char_id, std::string_view login_session, std::string_view world_token,
        std::string_view world_host, std::string_view fail_reason, std::uint16_t world_port);
    bool SendWorldListResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
        std::uint16_t count, const pt_la::WorldSummary* worlds, std::string_view fail_reason);
    bool SendWorldSelectResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
        std::uint16_t world_id, std::uint16_t channel_id, std::uint32_t world_server_id,
        std::string_view login_session, std::string_view world_host, std::string_view fail_reason,
        std::uint16_t world_port);
    bool SendWorldEnterSuccessNotify(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint64_t trace_id, std::uint64_t account_id, std::uint64_t char_id,
        std::string_view login_session, std::string_view world_token);
    bool SendCharacterListResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
        std::uint16_t count, const pt_la::CharacterSummary* characters, std::string_view fail_reason);
    bool SendCharacterSelectResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
        std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
        std::uint64_t char_id, std::string_view login_session, std::string_view world_token,
        std::string_view world_host, std::string_view fail_reason, std::uint16_t world_port);

protected:
    bool DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg) override;
    void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
    void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
    bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial) override;

private:
    dc::AccountLineRuntime& runtime_;
};
