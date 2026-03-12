#pragma once

#include <cstdint>
#include <functional>

#include "server_common/handler/service_line_handler_base.h"

class ControlHandler : public dc::ServiceLineHandlerBase
{
public:
    using WorldReadyFn = std::function<bool()>;

public:
    explicit ControlHandler(WorldReadyFn is_world_ready);
    ~ControlHandler() override = default;

protected:
    bool DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
        _MSG_HEADER* pMsgHeader, char* pMsg) override;

    void OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex,
        std::uint32_t dwSerial) override;
    void OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex,
        std::uint32_t dwSerial) override;
    bool ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial) override;

private:
    bool RequireWorldReady(std::uint16_t msg_type) const noexcept;

private:
    WorldReadyFn is_world_ready_;
};
