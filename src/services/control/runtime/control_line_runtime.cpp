#include "services/control/runtime/control_line_runtime.h"

#include <memory>

#include <spdlog/spdlog.h>

#include "server_common/runtime/line_client_start_helper.h"
#include "server_common/runtime/line_start_helper.h"
#include "server_common/session/session_key.h"

namespace dc {

    ControlLineRuntime::ControlLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port)
        : port_(port)
        , world_host_(std::move(world_host))
        , world_port_(world_port)
    {
    }

    bool ControlLineRuntime::IsWorldReady() const noexcept
    {
        return world_ready_.load(std::memory_order_acquire);
    }

    void ControlLineRuntime::MarkWorldRegistered(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port)
    {
        world_sid_.store(sid, std::memory_order_relaxed);
        world_serial_.store(serial, std::memory_order_relaxed);
        world_server_id_.store(server_id, std::memory_order_relaxed);
        world_ready_.store(true, std::memory_order_release);

        spdlog::info(
            "ControlLineRuntime world ready. sid={} serial={} server_id={} server_name={} listen_port={}",
            sid, serial, server_id, server_name, listen_port);
    }

    void ControlLineRuntime::MarkWorldDisconnected(
        std::uint32_t sid,
        std::uint32_t serial)
    {
        const auto cur_sid = world_sid_.load(std::memory_order_relaxed);
        const auto cur_serial = world_serial_.load(std::memory_order_relaxed);

        if (cur_sid != 0 && !dc::IsSameSessionKey(cur_sid, cur_serial, sid, serial)) {
            spdlog::debug(
                "ControlLineRuntime ignore stale world disconnect. sid={} serial={} current_sid={} current_serial={}",
                sid, serial, cur_sid, cur_serial);
            return;
        }

        world_ready_.store(false, std::memory_order_release);
        world_sid_.store(0, std::memory_order_relaxed);
        world_serial_.store(0, std::memory_order_relaxed);
        world_server_id_.store(0, std::memory_order_relaxed);

        spdlog::warn("ControlLineRuntime world not ready. sid={} serial={}", sid, serial);
    }

    bool ControlLineRuntime::OnRuntimeInit()
    {
        dc::InitHostedLineEntry(
            client_line_,
            0,
            "control-client",
            port_,
            false,
            0);

        auto control_handler = std::make_shared<ControlHandler>(
            [this]() noexcept { return IsWorldReady(); });

        if (!dc::StartHostedLine(
            client_line_,
            io_,
            control_handler,
            [](std::uint64_t, std::function<void()> fn) {
            if (fn) fn();
        }))
        {
            spdlog::error("ControlLineRuntime failed to start hosted line. port={}", port_);
            return false;
        }

        auto world_handler = std::make_shared<ControlWorldHandler>(
            [this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port) {
            MarkWorldRegistered(sid, serial, server_id, server_name, listen_port);
        },
            [this](std::uint32_t sid, std::uint32_t serial) {
            MarkWorldDisconnected(sid, serial);
        });

        world_handler->SetServerIdentity(
            2,
            "control",
            port_);

        dc::InitOutboundLineEntry(
            world_line_,
            1,
            "control-world",
            world_host_,
            world_port_,
            true,
            1000,
            10000);

        if (!dc::StartOutboundLine(
            world_line_,
            io_,
            world_handler,
            [](std::uint64_t, std::function<void()> fn) {
            if (fn) fn();
        }))
        {
            spdlog::error(
                "ControlLineRuntime failed to start outbound world line. remote={}:{}",
                world_host_,
                world_port_);
            return false;
        }

        spdlog::info(
            "ControlLineRuntime started. client_port={} world_remote={}:{}",
            port_,
            world_host_,
            world_port_);
        return true;
    }

    void ControlLineRuntime::OnBeforeIoStop()
    {
        world_ready_.store(false, std::memory_order_release);
    }

    void ControlLineRuntime::OnAfterIoStop()
    {
        world_line_.host.Stop();
        client_line_.host.Stop();

        world_ready_.store(false, std::memory_order_release);
        world_sid_.store(0, std::memory_order_relaxed);
        world_serial_.store(0, std::memory_order_relaxed);
        world_server_id_.store(0, std::memory_order_relaxed);

        spdlog::info("ControlLineRuntime stopped.");
    }

    void ControlLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point)
    {
        // 현재는 별도 main tick 없음
    }

} // namespace dc
