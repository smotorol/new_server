#include "net/tcp/tcp_client.h"
#include "net/tcp/tcp_session.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace net {
	std::atomic<std::uint32_t> TcpClient::next_client_index_{ 1 };

	void TcpClient::start(const std::string& host, std::uint16_t port)
	{
		host_ = host;
		port_ = port;
		manual_stop_ = false;

		if (started_) {
			return;
		}

		started_ = true;
		reconnect_pending_ = false;
		reset_backoff_();

		auto self = shared_from_this();
		asio::co_spawn(resolver_.get_executor(),
			[self]() -> asio::awaitable<void> {
			co_await self->connect_loop_();
		},
			asio::detached);
	}

	void TcpClient::stop()
	{
		manual_stop_ = true;
		started_ = false;
		reconnect_pending_ = false;

		reconnect_timer_.cancel();
		resolver_.cancel();

		std::shared_ptr<TcpSession> s;
		{
			std::lock_guard<std::mutex> lk(session_mtx_);
			s = session_;
			session_.reset();
		}

		if (s) {
			s->close();
		}
	}

	void TcpClient::reset_backoff_() noexcept
	{
		current_backoff_ms_ = reconnect_delay_ms_;
	}

	void TcpClient::grow_backoff_() noexcept
	{
		const auto doubled = current_backoff_ms_ * 2u;
		current_backoff_ms_ = std::min(doubled, reconnect_delay_max_ms_);
	}

	void TcpClient::schedule_reconnect_()
	{
		if (!auto_reconnect_ || manual_stop_ || !started_ || reconnect_pending_) {
			return;
		}

		reconnect_pending_ = true;

		auto self = shared_from_this();
		asio::co_spawn(reconnect_timer_.get_executor(),
			[self]() -> asio::awaitable<void> {
			co_await self->wait_reconnect_();
		},
			asio::detached);
	}

	asio::awaitable<void> TcpClient::wait_reconnect_()
	{
		try {
			spdlog::warn("TcpClient reconnect scheduled. remote={}:{} backoff_ms={}",
				host_, port_, current_backoff_ms_);

			reconnect_timer_.expires_after(std::chrono::milliseconds(current_backoff_ms_));
			co_await reconnect_timer_.async_wait(asio::use_awaitable);
		}
		catch (const boost::system::system_error&) {
			reconnect_pending_ = false;
			co_return;
		}

		reconnect_pending_ = false;

		if (manual_stop_ || !started_) {
			co_return;
		}

		auto self = shared_from_this();
		asio::co_spawn(resolver_.get_executor(),
			[self]() -> asio::awaitable<void> {
			co_await self->connect_loop_();
		},
			asio::detached);
	}

	asio::awaitable<void> TcpClient::connect_loop_()
	{
		if (manual_stop_ || !started_) {
			co_return;
		}

		try {
			spdlog::info("TcpClient connecting. remote={}:{}", host_, port_);

			// 1) resolve
			auto results = co_await resolver_.async_resolve(host_, std::to_string(port_), asio::use_awaitable);

			// 2) connect할 socket 생성 (중요: TcpSession을 만들기 전에 connect해야 함)
			tcp::socket socket(resolver_.get_executor());
			co_await asio::async_connect(socket, results, asio::use_awaitable);

			// 3) 연결 성공 후에 session 생성 (서버 accept 흐름과 동일)
			std::shared_ptr<TcpSession> new_session;
			{
				std::lock_guard<std::mutex> lk(session_mtx_);
				if (client_index_ == 0) client_index_ = next_client_index_++;
				serial_++;
				new_session = std::make_shared<TcpSession>(
					std::move(socket),
					client_index_,
					serial_,
					handler_);
				session_ = new_session;
			}

			reset_backoff_();

			spdlog::info("TcpClient connected. remote={}:{} index={} serial={}",
				host_, port_, new_session->index(), new_session->serial());

			// 4) 세션 start는 awaitable이므로 세션 executor에서 돌림
			asio::co_spawn(resolver_.get_executor(),
				[self = shared_from_this(), s = std::move(new_session)]() -> asio::awaitable<void> {
				co_await s->start();

				// session 종료 후 현재 세션 포인터 정리
				{
					std::lock_guard<std::mutex> lk(self->session_mtx_);
					auto cur = self->session_;
					if (cur && cur.get() == s.get()) {
						self->session_.reset();
					}
				}

				if (!self->manual_stop_ && self->started_ && self->auto_reconnect_) {
					self->grow_backoff_();
					self->schedule_reconnect_();
				}

				co_return;
			},
				asio::detached);
		}
		catch (const std::exception& e) {
			spdlog::error("TcpClient connect failed. remote={}:{} what={}", host_, port_, e.what());

			{
				std::lock_guard<std::mutex> lk(session_mtx_);
				session_.reset();
			}

			if (!manual_stop_ && started_ && auto_reconnect_) {
				grow_backoff_();
				schedule_reconnect_();
			}
		}

		co_return;
	}
}
