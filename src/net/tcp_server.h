#pragma once
#include "tcp_session.h"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <algorithm>

namespace net {
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    // ✅ socketIndex 재사용 + serial 체크 구조
    // - index: 슬롯 인덱스 (재사용됨)
    // - serial: 해당 슬롯이 재연결될 때마다 증가 (stale 응답/close/send 방지)
    class TcpServer
    {
    public:
        TcpServer(asio::io_context& io, std::uint16_t port, HandlerPtr handler)
            : io_(io)
            , acceptor_(asio::make_strand(io_))
            , handler_(std::move(handler))
        {
            tcp::endpoint ep(tcp::v4(), port);
            acceptor_.open(ep.protocol());
            acceptor_.set_option(asio::socket_base::reuse_address(true));
            acceptor_.bind(ep);
            acceptor_.listen();
        }

        // sid로 세션 종료(레거시 CloseSocket 대체)
        void close(std::uint32_t index, std::uint32_t serial)
        {
            if (auto s = try_get_session_(index, serial)) {
                s->close();
            }
        }

        // (관리/테스트용) serial 없이 강제 종료
        void close(std::uint32_t index)
        {
            std::lock_guard<std::mutex> lk(slots_mtx_);
            if (index >= slots_.size()) return;
            if (auto s = slots_[index].lock()) s->close();
        }

        // index/serial로 송신(레거시 Send 대체)
        // body는 nullable. header.m_wSize는 (헤더+바디) 전체 크기로 세팅되어 있어야 함.
        bool send(std::uint32_t index, std::uint32_t serial, const _MSG_HEADER& header, const char* body)
        {
            if (auto s = try_get_session_(index, serial)) {
                s->async_send(header, body);
                return true;
            }
            return false;
        }

		// ✅ Lossy send (AOI/bench/move): if the session send queue is saturated, DO NOT close; just report failure.
		bool try_send_lossy(std::uint32_t index, std::uint32_t serial, const _MSG_HEADER& header, const char* body)
		{
			if (auto s = try_get_session_(index, serial)) {
				const std::size_t msg_bytes = (std::size_t)header.m_wSize;
				if (!s->can_accept_send(msg_bytes)) return false;
				s->async_send_lossy(header, body);
				return true;
			}
			return false;
		}

        // (관리/테스트용) serial 없이 송신 (현재 슬롯에 매달린 세션으로 보냄)
		bool send(std::uint32_t index, const _MSG_HEADER& header, const char* body)
		{
			std::lock_guard<std::mutex> lk(slots_mtx_);
			if (index >= slots_.size()) return false;
			if (auto s = slots_[index].lock()) { s->async_send(header, body); return true; }
			return false;
		}

		// ===== stats helpers (thread-safe) =====
		std::size_t active_session_count() const
		{
			std::lock_guard<std::mutex> lk(slots_mtx_);
			std::size_t n = 0;
			for (auto& w : slots_) {
				if (!w.expired()) ++n;
			}
			return n;
		}

		struct SendqStats {
			std::size_t q_msgs_total = 0;
			std::size_t q_bytes_total = 0;
			std::size_t q_msgs_max = 0;
			std::size_t q_bytes_max = 0;
			std::uint64_t drops_total = 0;
		};

		SendqStats collect_sendq_stats() const
		{
			SendqStats st;
			std::lock_guard<std::mutex> lk(slots_mtx_);
			for (auto& w : slots_) {
				auto s = w.lock();
				if (!s) continue;
				const auto qm = s->send_q_msgs();
				const auto qb = s->send_q_bytes();
				st.q_msgs_total += qm;
				st.q_bytes_total += qb;
				st.q_msgs_max = std::max(st.q_msgs_max, qm);
				st.q_bytes_max = std::max(st.q_bytes_max, qb);
				st.drops_total += s->send_drop_count();
			}
			return st;
		}

        asio::awaitable<void> run()
        {
            for (;;) {
                tcp::socket socket = co_await acceptor_.async_accept(asio::use_awaitable);
                // 슬롯 할당(재사용) + serial 증가
                std::uint32_t index = 0;
                std::uint32_t serial = 0;
                std::shared_ptr<TcpSession> session;
                {
                    std::lock_guard<std::mutex> lk(slots_mtx_);
                    index = alloc_slot_();
                    serial = ++slot_serials_[index]; // 0은 reserved, 1부터 사용
                    session = std::make_shared<TcpSession>(std::move(socket), index, serial, handler_);
                    slots_[index] = session;
                }

                asio::co_spawn(acceptor_.get_executor(),
                    [this, session]() -> asio::awaitable<void> {
                        co_await session->start();

                        // 종료 후 registry 정리(같은 index라도 serial이 바뀌었으면 건드리지 않음)
                        {
                            std::lock_guard<std::mutex> lk(slots_mtx_);
                            const auto idx = session->index();
                            const auto ser = session->serial();

                            if (idx < slots_.size()) {
                                if (auto cur = slots_[idx].lock()) {
                                    if (cur->serial() == ser) {
                                        slots_[idx].reset();
                                        free_list_.push_back(idx);
                                        
                                    }
                                }
                                else {
                                    free_list_.push_back(idx);
                                }
                            }
                        }
                        co_return;
                    },
                    asio::detached);
            }
        }

    private:
		std::uint32_t alloc_slot_()
		{
			if (!free_list_.empty()) {
				auto idx = free_list_.back();
				free_list_.pop_back();
				return idx;
			}
			const std::uint32_t idx = static_cast<std::uint32_t>(slots_.size());
			slots_.push_back({});
			slot_serials_.push_back(0);
			return idx;
		}
		std::shared_ptr<TcpSession> try_get_session_(std::uint32_t index, std::uint32_t serial)
		{
			std::lock_guard<std::mutex> lk(slots_mtx_);
			if (index >= slots_.size()) return {};
			auto sp = slots_[index].lock();
			if (!sp) return {};
			if (sp->serial() != serial) return {};
			return sp;
		}

    private:
        asio::io_context& io_;
        tcp::acceptor acceptor_;
        HandlerPtr handler_;

        mutable std::mutex slots_mtx_;
        std::vector<std::weak_ptr<TcpSession>> slots_;
        std::vector<std::uint32_t> slot_serials_;
        std::vector<std::uint32_t> free_list_;
    };

} // namespace net
