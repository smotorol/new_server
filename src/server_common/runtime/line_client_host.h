#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <spdlog/spdlog.h>

#include "net/tcp/tcp_client.h"
#include "net/tcp/tcp_session.h"
#include "server_common/handler/service_line_handler_base.h"

namespace dc {

    class LineClientHost
    {
    public:
        struct Config
        {
            std::string name;
            std::string remote_host;
            std::uint16_t remote_port = 0;

            // 현재 TcpClient 구현에는 아직 실제 자동 재연결 로직이 없음.
            // 설정값은 보관만 하고, 로그 용도로만 둔다.
            bool auto_reconnect = false;
            std::uint32_t reconnect_delay_ms = 3000;

            std::function<void()> on_started;
            std::function<void()> on_stopped;
        };

    public:
        LineClientHost() = default;
        ~LineClientHost() = default;

        LineClientHost(const LineClientHost&) = delete;
        LineClientHost& operator=(const LineClientHost&) = delete;

        bool Start(
            boost::asio::io_context& io,
            Config config,
            std::shared_ptr<ServiceLineHandlerBase> handler,
            const std::function<void(std::uint64_t, std::function<void()>)>& dispatch)
        {
            Stop();

            config_ = std::move(config);
            handler_ = std::move(handler);

            if (!handler_) {
                spdlog::error("LineClientHost[{}] start failed: handler is null.", config_.name);
                return false;
            }

            handler_->AttachDispatcher(dispatch);

            client_ = std::make_shared<net::TcpClient>(io, handler_);
            handler_->AttachClient(client_);

            spdlog::info(
                "LineClientHost[{}] starting. remote={}:{} auto_reconnect={} reconnect_delay_ms={}",
                config_.name,
                config_.remote_host,
                config_.remote_port,
                config_.auto_reconnect,
                config_.reconnect_delay_ms);

            client_->start(config_.remote_host, config_.remote_port);

            if (config_.on_started) {
                config_.on_started();
            }

            return true;
        }

        void Stop()
        {
            if (client_) {
                spdlog::info("LineClientHost[{}] stopping.", config_.name);

                if (auto s = client_->session()) {
                    s->close();
                }

                client_.reset();
            }

            handler_.reset();

            if (config_.on_stopped) {
                config_.on_stopped();
            }
        }

        [[nodiscard]] net::TcpClient* client() const noexcept
        {
            return client_.get();
        }

        [[nodiscard]] std::shared_ptr<net::TcpClient> shared_client() const noexcept
        {
            return client_;
        }

    private:
        Config config_{};
        std::shared_ptr<ServiceLineHandlerBase> handler_;
        std::shared_ptr<net::TcpClient> client_;
    };

} // namespace dc
