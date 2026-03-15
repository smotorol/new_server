#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <mutex>

#include <spdlog/spdlog.h>

#include "net/handler/net_handler.h"
#include "net/tcp/tcp_server.h"
#include "net/tcp/tcp_client.h"

namespace dc {

	// ✅ server/test_client 공용 NetworkEX 베이스
	//	- dispatcher(post)가 있으면 "로직 스레드(Actor)"로 넘김
	//	- packet pointer 수명 문제 해결(복사 후 Post)
	//	- server/client 송신 API 통합
	//	- serial 안전성용 session_serial 최신값 유지
	class NetworkEXBase : public net::INetHandler
		, public std::enable_shared_from_this<NetworkEXBase>
	{
	public:
		explicit NetworkEXBase(std::uint32_t pro_id = 0) : pro_id_(pro_id) {}
		~NetworkEXBase() override = default;

		void SetProId(std::uint32_t pro_id) { pro_id_ = pro_id; }
		std::uint32_t pro_id() { return pro_id_; }

		// ✅ Actor 모델용 dispatcher
		// - actor_id(= char_id 등) 단위로 "순서 + 단일 실행" 보장
		using Dispatcher = std::function<void(std::uint64_t /*actor_id*/, std::function<void()>)>;
		void AttachDispatcher(Dispatcher post) { post_ = std::move(post); }
		bool HasDispatcher() const noexcept { return static_cast<bool>(post_); }

		// ✅ server 모드: TcpServer를 attach (send/close 가능)
		void AttachServer(net::TcpServer* server) { server_ = server; }

		// ✅ client 모드: TcpClient를 attach (send 가능)
		void AttachClient(std::weak_ptr<net::TcpClient> client) { client_ = std::move(client); }

		// ========== 레거시 스타일 API 유지(서버/클라 공용) ==========
		void Close(std::uint32_t /*dwProID*/, std::uint32_t dwSocketIndex, std::uint32_t dwSerial, bool /*bSlowClose*/ = false)
		{
			// server 모드면 serial 체크 close
			if (server_) {
				server_->close(dwSocketIndex, dwSerial);
				return;
			}

			// client 모드면 현재 세션 close (serial mismatch면 drop)
			std::lock_guard<std::mutex> lk(session_mtx_);
			auto it = session_serials_.find(dwSocketIndex);
			if (it != session_serials_.end() && it->second != dwSerial) return;
			if (auto c = client_.lock()) {
				if (auto s = c->session()) s->close();
			}
		}

		bool Send(std::uint32_t /*dwProID*/, std::uint32_t dwSocketIndex, std::uint32_t dwSerial, const _MSG_HEADER& header, const char* body)
		{
			// server 모드
			if (server_) {
				return server_->send(dwSocketIndex, dwSerial, header, body);
			}
			// client 모드(보통 index=0 하나만 씀)
			std::lock_guard<std::mutex> lk(session_mtx_);
			auto it = session_serials_.find(dwSocketIndex);
			if (it != session_serials_.end() && it->second != dwSerial) return false;
			if (auto c = client_.lock()) {
				if (auto s = c->session()) {
					s->async_send(header, body);
					return true;
				}

			}
			return false;
		}

		// serial 없이 강제 송신(테스트/관리용)
		bool SendUnsafe(std::uint32_t /*dwProID*/, std::uint32_t dwSocketIndex, const _MSG_HEADER& header, const char* body)
		{
			if (server_) return server_->send(dwSocketIndex, header, body);
			if (auto c = client_.lock()) {
				if (auto s = c->session()) {
					s->async_send(header, body);
					return true;
				}
			}
			return false;
		}

		// ✅ Lossy send: used for high-frequency AOI/move/bench packets.
		// - If the underlying session send queue is full, it returns false and does NOT disconnect.
		bool TrySendLossy(std::uint32_t /*dwProID*/, std::uint32_t dwSocketIndex, std::uint32_t dwSerial,
			const _MSG_HEADER& header, const char* body)
		{
			// server mode
			if (server_) {
				return server_->try_send_lossy(dwSocketIndex, dwSerial, header, body);
			}

			// client mode
			std::lock_guard<std::mutex> lk(session_mtx_);
			auto it = session_serials_.find(dwSocketIndex);
			if (it != session_serials_.end() && it->second != dwSerial) return false;
			if (auto c = client_.lock()) {
				if (auto s = c->session()) {
					const std::size_t msg_bytes = (std::size_t)header.m_wSize;
					if (!s->can_accept_send(msg_bytes)) return false;
					s->async_send_lossy(header, body);
					return true;
				}
			}
			return false;
		}

		// ========== net::INetHandler ==========
		void on_connected(std::uint32_t idx, std::uint32_t serial) override
		{
			if (!post_) {
				spdlog::critical("[NET] Dispatcher NOT set! on_connected denied. pro={}, idx={}, serial={}", pro_id_, idx, serial);
				return;
			}
			const std::uint64_t actor = ResolveActorId(idx);
			dispatch_(actor, [self = shared_from_this(), idx, serial] {
				self->HandleConnected_(idx, serial); });
		}

		void on_disconnected(std::uint32_t idx, std::uint32_t serial) override
		{
			if (!post_) {
				spdlog::critical("[NET] Dispatcher NOT set! on_disconnected ignored. pro={}, idx={}, serial={}", pro_id_, idx, serial);
				return;
			}

			const std::uint64_t actor = ResolveActorId(idx);
			dispatch_(actor, [self = shared_from_this(), idx, serial] {
				self->HandleDisconnected_(idx, serial); });
		}

		bool on_packet(std::uint32_t idx, std::uint32_t serial, const _MSG_HEADER* header, const char* body) override
		{
			// ✅ "패킷 처리는 무조건 로직 스레드(Actor)" 강제
			if (!post_) {
				spdlog::critical("[NET] Dispatcher NOT set! on_packet denied => disconnect. pro={}, idx={}, serial={}, size={}",
					pro_id_, idx, serial, header ? header->m_wSize : 0);
				return false;
			}
			return dispatch_packet_(idx, serial, header, body);
		}

	protected:
		// ====== 파생 클래스가 구현할 “디스패처 부분” ======
		virtual bool DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex
			, _MSG_HEADER* pMsgHeader, char* pMsg) = 0;
		virtual void AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex
			, std::uint32_t dwSerial) {}
		virtual void CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex
			, std::uint32_t dwSerial) {}

		// ✅ 파생 클래스에서 최신 serial이 필요할 때 사용(쓰레드 세이프)
		// - world 라인에서 DQS payload에 serial을 넣는 용도 등
		std::uint32_t GetLatestSerial(std::uint32_t idx) const
		{
			std::lock_guard<std::mutex> lk(session_mtx_);
			auto it = session_serials_.find(idx);
			return (it != session_serials_.end()) ? it->second : 0;
		}

		// ✅ ActorId 라우팅 키
		// - 기본: session index
		// - 캐릭터 단위 Actor로 바꾸려면: (sid -> char_id 바인딩 후) char_id 반환
		virtual std::uint64_t ResolveActorId(std::uint32_t session_idx) const { return static_cast<std::uint64_t>(session_idx); }
		
		// ✅ 패킷 단위로 Actor 라우팅을 바꿀 수 있는 훅
		// - default_actor는 ResolveActorId(session_idx) 결과
		// - io thread에서 호출될 수 있으므로, 여기서 읽는 상태는 thread-safe 해야 함
		virtual std::uint64_t ResolveActorIdForPacket(std::uint32_t session_idx,
			const _MSG_HEADER& header, const char* body, std::size_t body_len,
			std::uint64_t default_actor) const
		{
			(void)session_idx; (void)header; (void)body; (void)body_len;
			return default_actor;
		}
	private:
		void dispatch_(std::uint64_t actor_id, std::function<void()> fn)
		{
			if (post_) {
				post_(actor_id, std::move(fn));
				return;
			}
			spdlog::critical("[NET] Dispatcher NOT set! dispatch_ dropped. pro={}", pro_id_);
		}

		bool dispatch_packet_(std::uint32_t idx, std::uint32_t serial, const _MSG_HEADER* header, const char* body)
		{
			if (!header) return false;
			const std::uint16_t total = header->m_wSize;
			const std::size_t body_len = (total > MSG_HEADER_SIZE) ? (total - MSG_HEADER_SIZE) : 0;

			// ✅ ActorId는 "포스트 전에" 결정
			const std::uint64_t default_actor = ResolveActorId(idx);
			const std::uint64_t actor = ResolveActorIdForPacket(idx, *header, body, body_len, default_actor);
			auto hdr_copy = *header;
			constexpr std::size_t kSmall = 256;
			if (body_len <= kSmall)
			{
				std::array<std::uint8_t, kSmall> sbuf{};
				if (body_len && body) std::memcpy(sbuf.data(), body, body_len);
				dispatch_(actor, [self = shared_from_this(), idx, serial, hdr_copy, sbuf, body_len]() mutable {
					const char* b = (body_len == 0) ? nullptr : reinterpret_cast<const char*>(sbuf.data());
					const bool ok = self->HandlePacket_(idx, serial, &hdr_copy, b);
					if (!ok) {
						// ✅ 로직 스레드에서 패킷 거부 시 세션 종료(레거시 의미 유지)
						self->Close(self->pro_id_, idx, serial, /*bSlowClose=*/false);
					}
				});
			}
			else
			{
				auto buf = std::make_shared<std::vector<std::uint8_t>>();
				buf->resize(body_len);
				if (body_len && body) std::memcpy(buf->data(), body, body_len);
				dispatch_(actor, [self = shared_from_this(), idx, serial, hdr_copy, buf]() mutable {
					const bool ok = self->HandlePacket_(idx, serial, &hdr_copy,
					buf->empty() ? nullptr : reinterpret_cast<const char*>(buf->data()));
				if (!ok) {
					// ✅ 로직 스레드에서 패킷 거부 시 세션 종료(레거시 의미 유지)
					self->Close(self->pro_id_, idx, serial, /*bSlowClose=*/false);
				}
				});
			}
			return true;
		}

		void HandleConnected_(std::uint32_t idx, std::uint32_t serial)
		{
			{ std::lock_guard<std::mutex> lk(session_mtx_); session_serials_[idx] = serial; }
			AcceptClientCheck(pro_id_, idx, serial);
		}

		void HandleDisconnected_(std::uint32_t idx, std::uint32_t serial)
		{
			CloseClientCheck(pro_id_, idx, serial);
			// disconnect는 "최신 serial"만 정리 (stale disconnect 방지)
			std::lock_guard<std::mutex> lk(session_mtx_);
			auto it = session_serials_.find(idx);
			if (it != session_serials_.end() && it->second == serial) {
				session_serials_.erase(it);
			}
		}

		bool HandlePacket_(std::uint32_t idx, std::uint32_t serial, const _MSG_HEADER* header, const char* body)
		{
			// ✅ 패킷이 들어오는 시점의 serial을 최신값으로 유지 (응답/close 안전성)
			{ std::lock_guard<std::mutex> lk(session_mtx_); session_serials_[idx] = serial; }
			return DataAnalysis(pro_id_, idx,
				const_cast<_MSG_HEADER*>(header),
				const_cast<char*>(body));
		}

	private:
		std::uint32_t pro_id_ = 0;
		Dispatcher post_;
		net::TcpServer* server_ = nullptr;              // server 모드
		std::weak_ptr<net::TcpClient> client_;          // client 모드
		mutable std::mutex session_mtx_;
		std::unordered_map<std::uint32_t, std::uint32_t> session_serials_;
	};
} // namespace dc
