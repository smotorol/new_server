#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include "server_common/runtime/line_client_start_helper.h"
#include "server_common/runtime/line_registry.h"
#include "services/control/handler/control_handler.h"
#include "services/control/handler/control_world_handler.h"
#include "services/runtime/server_runtime_base.h"

namespace dc {

    class ControlLineRuntime final : public ServerRuntimeBase {
    public:
        ControlLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port);
        ~ControlLineRuntime() override = default;

        bool IsWorldReady() const noexcept;

    private:
        bool OnRuntimeInit() override;
        void OnBeforeIoStop() override;
        void OnAfterIoStop() override;
        void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

        void MarkWorldRegistered(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint32_t server_id,
            std::string_view server_name,
            std::uint16_t listen_port);

        void MarkWorldDisconnected(
            std::uint32_t sid,
            std::uint32_t serial);

    private:
        std::uint16_t port_ = 0;
        std::string world_host_ = "127.0.0.1";
        std::uint16_t world_port_ = 0;

        std::atomic<bool> world_ready_{ false };
        std::atomic<std::uint32_t> world_sid_{ 0 };
        std::atomic<std::uint32_t> world_serial_{ 0 };
        std::atomic<std::uint32_t> world_server_id_{ 0 };

        HostedLineEntry client_line_{};
        OutboundLineEntry world_line_{};
    };

} // namespace dc
