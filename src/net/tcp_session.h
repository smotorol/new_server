#pragma once
#include "net_handler.h"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>

#include <deque>
#include <atomic>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace net {
	namespace asio = boost::asio;
	using tcp = asio::ip::tcp;

	class TcpSession : public std::enable_shared_from_this<TcpSession>
	{
	public:
		TcpSession(tcp::socket socket,
			std::uint32_t session_index,
			std::uint32_t session_serial,
			HandlerPtr handler,
			std::uint32_t max_packet_bytes = 64 * 1024,
			std::size_t max_send_queue_msgs = 4096,
			std::size_t max_send_queue_bytes = 8 * 1024 * 1024)
			: socket_(std::move(socket))
			, strand_(asio::make_strand(socket_.get_executor()))
			, session_index_(session_index)
			, session_serial_(session_serial)
			, handler_(std::move(handler))
			, max_packet_bytes_(max_packet_bytes)
			, max_send_queue_msgs_(max_send_queue_msgs)
			, max_send_queue_bytes_(max_send_queue_bytes)
		{}

		std::uint32_t index() const noexcept { return session_index_; }
		std::uint32_t serial() const noexcept { return session_serial_; }

		asio::awaitable<void> start()
		{
			if (handler_) handler_->on_connected(session_index_, session_serial_);

			try {
				co_await read_loop();
			}
			catch (...) {
				// read 실패/예외 -> 종료
			}

			close();
			if (handler_) handler_->on_disconnected(session_index_, session_serial_);
			co_return;
		}

		// 기존 프로토콜(_MSG_HEADER) 그대로 송신
		void async_send(const _MSG_HEADER& header, const char* body /*nullable*/)
		{
			// 헤더 복사 + body 복사 (IOCP처럼 버퍼 생명주기 보장)
			const std::uint16_t total_size = header.m_wSize;
			if (total_size < MSG_HEADER_SIZE || total_size > max_packet_bytes_) {
				// 잘못된 패킷이면 무시
				return;
			}

			OutMsg m;
			m.bytes.resize(total_size);
			std::memcpy(m.bytes.data(), &header, MSG_HEADER_SIZE);
			const std::size_t body_len = static_cast<std::size_t>(total_size - MSG_HEADER_SIZE);
			if (body_len > 0 && body) {
				std::memcpy(m.bytes.data() + MSG_HEADER_SIZE, body, body_len);
			}

			asio::dispatch(strand_, [self = shared_from_this(), msg = std::move(m)]() mutable {
				self->enqueue_send_close_(std::move(msg));
			});
		}

		void close()
		{
			asio::dispatch(strand_, [self = shared_from_this()] {
				boost::system::error_code ec;
				self->socket_.shutdown(tcp::socket::shutdown_both, ec);
				self->socket_.close(ec);
			});
		}

	private:
		struct OutMsg {
			std::vector<std::uint8_t> bytes;
		};

		// ✅ Approximate queue stats for cross-thread backpressure checks
		std::atomic<std::size_t> send_q_msgs_atomic_{ 0 };
		std::atomic<std::size_t> send_q_bytes_atomic_{ 0 };
		std::atomic<std::uint64_t> send_drop_count_{ 0 }; // lossy send drops
		std::atomic<std::uint64_t> send_drop_bytes_{ 0 };
		std::atomic<std::uint64_t> app_tx_bytes_{ 0 };
		std::atomic<std::uint64_t> app_rx_bytes_{ 0 };

	public:
		// ✅ Non-blocking check (approx): is there room to enqueue msg_bytes?
		bool can_accept_send(std::size_t msg_bytes) const noexcept
		{
			const auto qn = send_q_msgs_atomic_.load(std::memory_order_relaxed);
			const auto qb = send_q_bytes_atomic_.load(std::memory_order_relaxed);
			if (qn + 1 > max_send_queue_msgs_) return false;
			if (qb + msg_bytes > max_send_queue_bytes_) return false;
			return true;
		}

		// ===== queue stats (approx) =====
		std::size_t send_q_msgs() const noexcept { return send_q_msgs_atomic_.load(std::memory_order_relaxed); }
		std::size_t send_q_bytes() const noexcept { return send_q_bytes_atomic_.load(std::memory_order_relaxed); }
		std::uint64_t send_drop_count() const noexcept { return send_drop_count_.load(std::memory_order_relaxed); }
		std::uint64_t send_drop_bytes() const noexcept { return send_drop_bytes_.load(std::memory_order_relaxed); }
		std::uint64_t app_tx_bytes() const noexcept { return app_tx_bytes_.load(std::memory_order_relaxed); }
		std::uint64_t app_rx_bytes() const noexcept { return app_rx_bytes_.load(std::memory_order_relaxed); }

		// ✅ Lossy send: if queue is full, DROP instead of closing the socket.
		void async_send_lossy(const _MSG_HEADER& header, const char* body /*nullable*/)
		{
			const std::uint16_t total_size = header.m_wSize;
			if (total_size < MSG_HEADER_SIZE || total_size > max_packet_bytes_) return;

			OutMsg m;
			m.bytes.resize(total_size);
			std::memcpy(m.bytes.data(), &header, MSG_HEADER_SIZE);
			const std::size_t body_len = static_cast<std::size_t>(total_size - MSG_HEADER_SIZE);
			if (body_len > 0 && body) {
				std::memcpy(m.bytes.data() + MSG_HEADER_SIZE, body, body_len);
			}

			asio::dispatch(strand_, [self = shared_from_this(), msg = std::move(m)]() mutable {
				self->enqueue_send_drop_(std::move(msg));
			});
		}

	private:
		// Reliable path: overflow => close (legacy behavior)
		void enqueue_send_close_(OutMsg msg)
		{
			const std::size_t msg_bytes = msg.bytes.size();

			if (send_q_.size() + 1 > max_send_queue_msgs_ || (send_q_bytes_ + msg_bytes) > max_send_queue_bytes_) {
				boost::system::error_code ec;
				socket_.close(ec);
				return;
			}

			const bool write_in_progress = !send_q_.empty();
			send_q_bytes_ += msg_bytes;
			send_q_.push_back(std::move(msg));
			send_q_msgs_atomic_.store(send_q_.size(), std::memory_order_relaxed);
			send_q_bytes_atomic_.store(send_q_bytes_, std::memory_order_relaxed);

			if (!write_in_progress) {
				asio::co_spawn(strand_,
					[self = shared_from_this()]() -> asio::awaitable<void> {
					co_await self->write_loop();
					co_return;
				},
					asio::detached);
			}
		}

		// Lossy path: overflow => DROP
		void enqueue_send_drop_(OutMsg msg)
		{
			const std::size_t msg_bytes = msg.bytes.size();

			if (send_q_.size() + 1 > max_send_queue_msgs_ || (send_q_bytes_ + msg_bytes) > max_send_queue_bytes_) {
				// drop newest
				send_drop_count_.fetch_add(1, std::memory_order_relaxed);
				send_drop_bytes_.fetch_add(static_cast<std::uint64_t>(msg_bytes), std::memory_order_relaxed);
				return;
			}

			const bool write_in_progress = !send_q_.empty();
			send_q_bytes_ += msg_bytes;
			send_q_.push_back(std::move(msg));
			send_q_msgs_atomic_.store(send_q_.size(), std::memory_order_relaxed);
			send_q_bytes_atomic_.store(send_q_bytes_, std::memory_order_relaxed);

			if (!write_in_progress) {
				asio::co_spawn(strand_,
					[self = shared_from_this()]() -> asio::awaitable<void> {
					co_await self->write_loop();
					co_return;
				},
					asio::detached);
			}
		}
		asio::awaitable<void> write_loop()
		{
			try {
				while (!send_q_.empty()) {
					OutMsg& m = send_q_.front();
					co_await asio::async_write(socket_, asio::buffer(m.bytes), asio::use_awaitable);
					app_tx_bytes_.fetch_add(static_cast<std::uint64_t>(m.bytes.size()), std::memory_order_relaxed);

					send_q_bytes_ -= m.bytes.size();
					send_q_.pop_front();

					send_q_msgs_atomic_.store(send_q_.size(), std::memory_order_relaxed);
					send_q_bytes_atomic_.store(send_q_bytes_, std::memory_order_relaxed);
				}
			}
			catch (...) {
				boost::system::error_code ec;
				socket_.close(ec);
			}
			co_return;
		}

		asio::awaitable<void> read_loop()
		{
			std::array<std::uint8_t, MSG_HEADER_SIZE> hbuf{};

			for (;;) {
				// 1) 헤더 4바이트
				co_await asio::async_read(socket_, asio::buffer(hbuf), asio::use_awaitable);

				_MSG_HEADER hdr{};
				std::memcpy(&hdr, hbuf.data(), MSG_HEADER_SIZE);

				const std::uint16_t total_size = hdr.m_wSize; // 헤더 포함
				if (total_size < MSG_HEADER_SIZE) {
					throw std::runtime_error("invalid packet size (too small)");
				}
				if (total_size > max_packet_bytes_) {
					throw std::runtime_error("invalid packet size (too big)");
				}

				const std::size_t body_len = static_cast<std::size_t>(total_size - MSG_HEADER_SIZE);
				body_.resize(body_len);

				if (body_len > 0) {
					co_await asio::async_read(socket_, asio::buffer(body_), asio::use_awaitable);
				}

				app_rx_bytes_.fetch_add(static_cast<std::uint64_t>(total_size), std::memory_order_relaxed);

				if (handler_) {
					const char* body_ptr = body_len ? reinterpret_cast<const char*>(body_.data()) : nullptr;
					const bool ok = handler_->on_packet(session_index_, session_serial_, &hdr, body_ptr);
					if (!ok) {
						throw std::runtime_error("handler rejected packet");
					}
				}
			}
		}

	private:
		tcp::socket socket_;
		asio::strand<asio::any_io_executor> strand_;

		std::uint32_t session_index_;
		std::uint32_t session_serial_;
		HandlerPtr handler_;

		std::uint32_t max_packet_bytes_;

		std::size_t max_send_queue_msgs_;
		std::size_t max_send_queue_bytes_;
		std::size_t send_q_bytes_ = 0;
		std::deque<OutMsg> send_q_;

		std::vector<std::uint8_t> body_;
	};

} // namespace net
