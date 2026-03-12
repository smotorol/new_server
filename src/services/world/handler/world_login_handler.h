#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "server_common/handler/service_line_handler_base.h"
#include "services/world/runtime/i_world_runtime.h"

class WorldLoginHandler : public dc::ServiceLineHandlerBase
{
public:
    using RegisterCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port)>;

    using UnregisterCallback = std::function<void(
        std::uint32_t sid,
        std::uint32_t serial)>;

    WorldLoginHandler(
        svr::IWorldRuntime& runtime,
        RegisterCallback on_register,
        UnregisterCallback on_unregister);
    ~WorldLoginHandler() override = default;

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
    svr::IWorldRuntime& runtime_;
    RegisterCallback on_register_;
    UnregisterCallback on_unregister_;
};
