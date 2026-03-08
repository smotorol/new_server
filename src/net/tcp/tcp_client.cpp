#include "../tcp/tcp_client.h"
#include "../tcp/tcp_session.h"


namespace net {
	std::atomic<std::uint32_t> TcpClient::next_client_index_{ 1 };

	void TcpClient::start(const std::string& host, std::uint16_t port)
	{
		auto self = shared_from_this();
		asio::co_spawn(resolver_.get_executor(),
			[self, host, port]() mutable -> asio::awaitable<void> {
				co_await self->connect_and_run_(std::move(host), port);
			},
			asio::detached);
	}

	asio::awaitable<void> TcpClient::connect_and_run_(std::string host, std::uint16_t port)
	{
		// 1) resolve
		auto results = co_await resolver_.async_resolve(host, std::to_string(port), asio::use_awaitable);

		// 2) connect할 socket 생성 (중요: TcpSession을 만들기 전에 connect해야 함)
		tcp::socket socket(resolver_.get_executor());
		co_await asio::async_connect(socket, results, asio::use_awaitable);

		// 3) 연결 성공 후에 session 생성 (서버 accept 흐름과 동일)
		{
			std::lock_guard<std::mutex> lk(session_mtx_);
			if (client_index_ == 0) client_index_ = next_client_index_++;
			serial_++;
			session_ = std::make_shared<TcpSession>(std::move(socket), client_index_, serial_, handler_);
		}

		// 4) 세션 start는 awaitable이므로 세션 executor에서 돌림
		asio::co_spawn(resolver_.get_executor(),
			[s = session_]() -> asio::awaitable<void> {
				co_await s->start();
			},
			asio::detached);

		co_return;
	}
}
