#pragma once

#include <cstdint>
#include <string>

#include "server_common/handler/service_line_handler_base.h"

namespace svr { class WorldRuntime; }

class WorldControlHandler : public dc::ServiceLineHandlerBase
{
public:
    explicit WorldControlHandler(svr::WorldRuntime& runtime);
    ~WorldControlHandler() override = default;

protected:
    bool DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
        _MSG_HEADER* pMsgHeader, char* pMsg) override;

    void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex,
        std::uint32_t dwSerial) override;
    void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex,
        std::uint32_t dwSerial) override;
    bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial) override;

private:
    bool SendRegisterAck(
        std::uint32_t dwProID,
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port,
        bool accepted);

private:
    svr::WorldRuntime& runtime_;
};
