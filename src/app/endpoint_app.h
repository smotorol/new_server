#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include "../net/tcp_server.h"
#include "../net/tcp_client.h"
#include "networkex_base.h"

namespace dc {

	enum class EndpointRole : std::uint8_t {
		Server = 0, // listen
		Client = 1, // connect

	};

	struct EndpointConfig {
		EndpointRole role = EndpointRole::Server;
		std::string  host = "127.0.0.1"; // Client일 때 사용
		std::uint16_t port = 0;
		std::uint32_t net_threads = 1;   // io_context.run 스레드 수
		bool start_logic_thread = true;  // dispatcher용 단일 writer 스레드(기본 on)
	};

	// ✅ listen/connect 플래그로 서버/클라를 통일 실행하는 런처
	// - NetworkEXBase에 dispatcher(post)를 자동 연결
	// - Server: TcpServer + accept 루프 co_spawn
	// - Client: TcpClient.start(host,port)
	class EndpointApp {
	public:
		using HandlerPtr = std::shared_ptr<dc::NetworkEXBase>;

		explicit EndpointApp(EndpointConfig cfg, HandlerPtr handler);
		~EndpointApp();

		EndpointApp(const EndpointApp&) = delete;
		EndpointApp& operator=(const EndpointApp&) = delete;

		void start();
		void stop();

		boost::asio::io_context& io() { return io_; }

		// (옵션) 외부에서 강제로 단일-writer 큐에 올리고 싶을 때
		void post(std::function<void()> fn);

		std::shared_ptr<net::TcpClient> client() const { return client_; }
		net::TcpServer* server() const { return server_.get(); }

	private:
		void start_logic_thread_();
		void logic_loop_();

	private:
		EndpointConfig cfg_;
		HandlerPtr handler_;

		boost::asio::io_context io_;
		std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;

		std::unique_ptr<net::TcpServer> server_;
		std::shared_ptr<net::TcpClient> client_;

		std::vector<std::thread> net_threads_;

		// 단일 writer 큐
		std::atomic<bool> running_{ false };
		std::thread logic_thread_;
		mutable std::mutex q_mtx_;
		std::condition_variable q_cv_;
		std::deque<std::function<void()>> q_;

	};


} // namespace dc
