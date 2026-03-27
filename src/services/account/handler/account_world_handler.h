#pragma once

#include <cstdint>
#include <string_view>

#include "server_common/handler/service_line_handler_base.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

namespace dc { class AccountLineRuntime; }

class AccountWorldHandler : public dc::ServiceLineHandlerBase
{
public:
    explicit AccountWorldHandler(dc::AccountLineRuntime& runtime);

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
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint16_t result_code,
        std::uint64_t account_id,
        std::uint64_t char_id,
        std::string_view login_session,
        std::string_view world_token);

    bool SendWorldCharacterListRequest(
        std::uint32_t dwProID,
        std::uint32_t dwIndex,
        std::uint32_t dwSerial,
        std::uint64_t trace_id,
        std::uint64_t request_id,
        std::uint64_t account_id,
        std::uint16_t world_id,
        std::string_view login_session);
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
    dc::AccountLineRuntime& runtime_;
};
