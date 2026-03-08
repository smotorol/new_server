#pragma once
#include <memory>
#include <atomic>
#include <string>
#include <mutex>
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
			, handler_(std::move(handler))
		{}

		// 비동기 시작 (내부에서 co_spawn)
		void start(const std::string& host, std::uint16_t port);

		std::shared_ptr<TcpSession> session() const {
			std::lock_guard<std::mutex> lk(session_mtx_);
			return session_;
		}

	private:
		asio::awaitable<void> connect_and_run_(std::string host, std::uint16_t port);

	private:
		asio::io_context& io_;
		tcp::resolver resolver_;
		HandlerPtr handler_;
		std::shared_ptr<TcpSession> session_;
		mutable std::mutex session_mtx_;

		std::uint32_t client_index_{ 0 }; // 프로세스 내에서 유일한 index
		std::uint32_t serial_{ 0 };      // 재연결 시 증가
		static std::atomic<std::uint32_t> next_client_index_;
	};
}
