#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <spdlog/spdlog.h>

#include "net/tcp/tcp_server.h"
#include "server_common/handler/service_line_handler_base.h"
#include "server_common/runtime/line_host_stats.h"

namespace dc {

	class LineHost {
	public:
		using DispatchFn = std::function<void(std::uint64_t, std::function<void()>)>;
		using SessionOpenPolicyFn = std::function<bool(std::uint32_t, std::uint64_t)>;
		using SessionHookFn = std::function<void(std::uint32_t, std::uint64_t)>;
		using SessionRejectHookFn = std::function<void(std::uint32_t, std::uint64_t, std::string_view)>;

		struct Config {
			std::uint16_t port = 0;
			std::string name;
			bool verbose_session_log = false;
			std::size_t max_sessions = 0; // 0 = unlimited
			SessionOpenPolicyFn session_open_policy;
			SessionHookFn on_session_open;
			SessionHookFn on_session_close;
			SessionRejectHookFn on_session_reject;
		};

	public:
		LineHost() = default;

		bool Start(boost::asio::io_context& io,
			Config cfg,
			std::shared_ptr<ServiceLineHandlerBase> handler,
			DispatchFn dispatch)
		{
			Stop();

			cfg_ = std::move(cfg);
			handler_ = std::move(handler);

			if (!handler_) {
				spdlog::error("LineHost[{}]: handler is null.", cfg_.name);
				return false;
			}

			server_ = std::make_unique<net::TcpServer>(io, cfg_.port, handler_);

			handler_->AttachServer(server_.get());
			handler_->AttachDispatcher(std::move(dispatch));
			handler_->SetSessionLifecycleHooks(
				[this](std::uint32_t socket_index, std::uint32_t /*serial*/) {
					return this->OnSessionOpen(socket_index);
				},
				[this](std::uint32_t socket_index, std::uint32_t /*serial*/) {
					this->OnSessionClose(socket_index);
				});

			boost::asio::co_spawn(io, server_->run(), boost::asio::detached);

			stats_.OnStart();

			spdlog::info(
				"LineHost[{}] started. port={}, verbose_session_log={}, max_sessions={}",
				cfg_.name,
				cfg_.port,
				cfg_.verbose_session_log,
				cfg_.max_sessions);
			return true;
		}

		void Stop()
		{
			if (!server_ && !handler_) {
				return;
			}

			spdlog::info("LineHost[{}] stopping. current_sessions={}, peak_sessions={}",
				cfg_.name,
				stats_.CurrentSessions(),
				stats_.PeakSessions());

			spdlog::info("LineHost[{}] summary. opens={}, closes={}, rejects={}, rejects_by_limit={}, rejects_by_policy={}",
				cfg_.name,
				stats_.session_open_count.load(std::memory_order_relaxed),
				stats_.session_close_count.load(std::memory_order_relaxed),
				stats_.session_reject_count.load(std::memory_order_relaxed),
				stats_.session_reject_by_limit_count.load(std::memory_order_relaxed),
				stats_.session_reject_by_policy_count.load(std::memory_order_relaxed));

			server_.reset();
			handler_.reset();

			stats_.OnStop();

			spdlog::info("LineHost[{}] stopped.", cfg_.name);
		}

		bool started() const noexcept
		{
			return static_cast<bool>(server_) && static_cast<bool>(handler_);
		}

		ServiceLineHandlerBase* handler() noexcept
		{
			return handler_.get();
		}

		const ServiceLineHandlerBase* handler() const noexcept
		{
			return handler_.get();
		}

		template <typename HandlerT>
		HandlerT* handler_as() noexcept
		{
			return dynamic_cast<HandlerT*>(handler_.get());
		}

		template <typename HandlerT>
		const HandlerT* handler_as() const noexcept
		{
			return dynamic_cast<const HandlerT*>(handler_.get());
		}

		net::TcpServer* server() noexcept
		{
			return server_.get();
		}

		const net::TcpServer* server() const noexcept
		{
			return server_.get();
		}

		const std::string& name() const noexcept
		{
			return cfg_.name;
		}

		std::uint16_t port() const noexcept
		{
			return cfg_.port;
		}

		const LineHostStats& stats() const noexcept
		{
			return stats_;
		}

		bool OnSessionOpen(std::uint32_t socket_index)
		{
			const auto cur = stats_.current_sessions.load(std::memory_order_relaxed);
			const auto next = cur + 1;

			if (cfg_.max_sessions > 0 && next > cfg_.max_sessions) {
				stats_.OnSessionRejectByLimit();

				if (cfg_.on_session_reject) {
					cfg_.on_session_reject(socket_index, cur, "max_sessions");
				}

				spdlog::warn(
					"LineHost[{}] session rejected(limit). socket_index={}, current_sessions={}, max_sessions={}",
					cfg_.name,
					socket_index,
					cur,
					cfg_.max_sessions);
				return false;
			}

			if (cfg_.session_open_policy && !cfg_.session_open_policy(socket_index, next)) {
				stats_.OnSessionRejectByPolicy();

				if (cfg_.on_session_reject) {
					cfg_.on_session_reject(socket_index, cur, "session_open_policy");
				}

				spdlog::warn(
					"LineHost[{}] session rejected(policy). socket_index={}, current_sessions={}, next_sessions={}",
					cfg_.name,
					socket_index,
					cur,
					next);
				return false;
			}

			stats_.OnSessionOpen();
			const auto now_cur = stats_.current_sessions.load(std::memory_order_relaxed);

			if (cfg_.on_session_open) {
				cfg_.on_session_open(socket_index, now_cur);
			}

			if (cfg_.verbose_session_log) {
				spdlog::info(
					"LineHost[{}] session opened. socket_index={}, current_sessions={}, peak_sessions={}",
					cfg_.name,
					socket_index,
					now_cur,
					stats_.peak_sessions.load(std::memory_order_relaxed));
			}

			return true;
		}

		void OnSessionClose(std::uint32_t socket_index)
		{
			stats_.OnSessionClose();
			const auto now_cur = stats_.current_sessions.load(std::memory_order_relaxed);

			if (cfg_.on_session_close) {
				cfg_.on_session_close(socket_index, now_cur);
			}

			if (cfg_.verbose_session_log) {
				spdlog::info(
					"LineHost[{}] session closed. socket_index={}, current_sessions={}",
					cfg_.name,
					socket_index,
					now_cur);
			}
		}

		const Config& config() const noexcept
		{
			return cfg_;
		}

		std::size_t max_sessions() const noexcept
		{
			return cfg_.max_sessions;
		}

	private:
		Config cfg_{};
		LineHostStats stats_{};

		std::shared_ptr<ServiceLineHandlerBase> handler_;
		std::unique_ptr<net::TcpServer> server_;
	};

} // namespace dc
