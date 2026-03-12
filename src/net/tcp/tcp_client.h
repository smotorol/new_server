#pragma once
#include <memory>
#include <atomic>
#include <string>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include "net/handler/net_handler.h"

namespace net {
	namespace asio = boost::asio;
	using tcp = asio::ip::tcp;

	class TcpSession;

	class TcpClient : public std::enable_shared_from_this<TcpClient>
	{
	public:
		TcpClient(asio::io_context& io, HandlerPtr handler)
			: io_(io)
			, resolver_(asio::make_strand(io_))
			, reconnect_timer_(asio::make_strand(io_))
			, handler_(std::move(handler))
		{}

		// 비동기 시작 (내부에서 co_spawn)
		void start(const std::string& host, std::uint16_t port);
		void stop();

		void set_auto_reconnect(bool enabled) noexcept { auto_reconnect_ = enabled; }
		void set_reconnect_delay_ms(std::uint32_t value) noexcept { reconnect_delay_ms_ = value; current_backoff_ms_ = value; }
		void set_reconnect_delay_max_ms(std::uint32_t value) noexcept { reconnect_delay_max_ms_ = value; }
		bool auto_reconnect() const noexcept { return auto_reconnect_; }
 
		std::shared_ptr<TcpSession> session() const {
			std::lock_guard<std::mutex> lk(session_mtx_);
			return session_;
		}

	private:
		asio::awaitable<void> connect_loop_();
		asio::awaitable<void> wait_reconnect_();
		void schedule_reconnect_();
		void reset_backoff_() noexcept;
		void grow_backoff_() noexcept;

	private:
		asio::io_context& io_;
		tcp::resolver resolver_;
		HandlerPtr handler_;
		std::shared_ptr<TcpSession> session_;
		mutable std::mutex session_mtx_;

		std::uint32_t client_index_{ 0 }; // 프로세스 내에서 유일한 index
		std::uint32_t serial_{ 0 };      // 재연결 시 증가
		static std::atomic<std::uint32_t> next_client_index_;

		std::string host_;
		std::uint16_t port_{ 0 };

		bool started_{ false };
		bool manual_stop_{ false };
		bool auto_reconnect_{ false };
		bool reconnect_pending_{ false };

		std::uint32_t reconnect_delay_ms_{ 1000 };
		std::uint32_t reconnect_delay_max_ms_{ 10000 };
		std::uint32_t current_backoff_ms_{ 1000 };

		asio::steady_timer reconnect_timer_;
	};
}
