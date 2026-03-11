#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <spdlog/spdlog.h>

#include "server_common/runtime/line_client_host.h"

namespace dc {

    struct OutboundLineDesc
    {
        std::uint32_t id = 0;
        std::string name;
        std::string remote_host;
        std::uint16_t remote_port = 0;
        bool auto_reconnect = false;
        std::uint32_t reconnect_delay_ms = 3000;
    };

    struct OutboundLineEntry
    {
        OutboundLineDesc desc{};
        LineClientHost host{};
    };

    inline void InitOutboundLineEntry(
        OutboundLineEntry& line,
        std::uint32_t id,
        std::string_view name,
        std::string_view remote_host,
        std::uint16_t remote_port,
        bool auto_reconnect = false,
        std::uint32_t reconnect_delay_ms = 3000)
    {
        line.desc.id = id;
        line.desc.name = name;
        line.desc.remote_host = remote_host;
        line.desc.remote_port = remote_port;
        line.desc.auto_reconnect = auto_reconnect;
        line.desc.reconnect_delay_ms = reconnect_delay_ms;
    }

    inline bool StartOutboundLine(
        OutboundLineEntry& line,
        boost::asio::io_context& io,
        std::shared_ptr<ServiceLineHandlerBase> handler,
        const std::function<void(std::uint64_t, std::function<void()>)>& dispatch)
    {
        return line.host.Start(
            io,
            dc::LineClientHost::Config{
                .name = line.desc.name,
                .remote_host = line.desc.remote_host,
                .remote_port = line.desc.remote_port,
                .auto_reconnect = line.desc.auto_reconnect,
                .reconnect_delay_ms = line.desc.reconnect_delay_ms,
                .on_started = [name = line.desc.name,
                               host = line.desc.remote_host,
                               port = line.desc.remote_port]() {
                    spdlog::info("OutboundLine[{}] start requested. remote={}:{}", name, host, port);
                },
                .on_stopped = [name = line.desc.name]() {
                    spdlog::info("OutboundLine[{}] stopped.", name);
                },
            },
            std::move(handler),
            dispatch);
    }

} // namespace dc
