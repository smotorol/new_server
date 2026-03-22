#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "server_common/runtime/line_client_start_helper.h"
#include "server_common/runtime/line_registry.h"
#include "services/login/handler/login_handler.h"
#include "services/login/handler/login_account_handler.h"
#include "services/runtime/login_auth_types.h"
#include "services/runtime/server_runtime_base.h"

namespace dc {

	// LoginLineRuntime 책임
	// - client(login client)와 account coordinator 사이의 진입 게이트웨이
	// - 실제 계정/캐릭터 식별의 진실 원천(source of truth)은 account 응답 payload
	// - world 입장 성공 최종 소유권은 world runtime이 가지며, login은 pending login session 정리만 수행
	class LoginLineRuntime final : public ServerRuntimeBase {
	public:
		LoginLineRuntime(
			std::uint16_t port,
			std::string account_host,
			std::uint16_t account_port);
		~LoginLineRuntime() override = default;

		bool IsWorldReady() const noexcept;

		bool IssueLoginRequest(
			std::uint32_t sid,
			std::uint32_t serial,
			std::string_view login_id,
			std::string_view password,
			std::uint64_t selected_char_id);

		void RemoveLoginSession(std::uint32_t sid, std::uint32_t serial);

	private:
		struct DuplicateSessionRef
		{
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;
		};

		struct PendingLoginRequest
		{
			std::uint64_t request_id = 0;
			std::uint32_t client_sid = 0;
			std::uint32_t client_serial = 0;
			std::string login_id;
			std::string password;
			std::uint64_t selected_char_id = 0;
			std::chrono::steady_clock::time_point issued_at{};

			std::string world_host;
			std::uint16_t world_port = 0;
		};

		// account coordinator line 연결/해제 상태만 관리한다.
		void MarkAccountRegistered(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t server_id,
			std::string_view server_name,
			std::uint16_t listen_port);

		void MarkAccountDisconnected(
			std::uint32_t sid,
			std::uint32_t serial);

		bool IsAccountReady() const noexcept;
		bool SendAccountAuthRequest_(const PendingLoginRequest& pending);

		// account가 확정한 인증 결과를 받아 client login session에 바인딩한다.
		void OnAccountAuthResult(
			std::uint64_t request_id,
			bool ok,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			std::string_view world_host,
			std::uint16_t world_port,
			std::string_view fail_reason);

		// world의 enter success notify를 받으면 login pending session을 정리한다.
		void OnWorldEnterSuccessNotify(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token);

		void CompleteLoginRequest_(
			PendingLoginRequest pending,
			bool ok,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			std::string_view fail_reason);

		bool IsValidAuthIdentity_(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token) const noexcept;

		bool SendLoginResultSuccess_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view token,
			std::string_view world_host,
			std::uint16_t world_port);

		void ExpirePendingLoginRequests_(std::chrono::steady_clock::time_point now);

		bool SendLoginResultFail_(
			std::uint32_t sid,
			std::uint32_t serial,
			const char* reason);

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

		void RemoveLoginSession_NoLock_(std::uint32_t sid, std::uint32_t serial);
		void EraseDetachedWorldEnterState_NoLock_(
			std::string_view login_session,
			std::string_view world_token);
		void AddDuplicateCandidateBySid_NoLock_(
			std::uint32_t sid,
			std::uint32_t new_sid,
			std::uint32_t new_serial,
			std::vector<DuplicateSessionRef>& out);

		std::vector<DuplicateSessionRef> CollectDuplicateSessions_NoLock_(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::uint32_t new_sid,
			std::uint32_t new_serial);

		void CloseDuplicateLoginSessions_(const std::vector<DuplicateSessionRef>& victims);

	private:
		std::uint16_t port_ = 0;
		std::string account_host_ = "127.0.0.1";
		std::uint16_t account_port_ = 0;

		std::mutex login_sessions_mtx_;
		std::mutex pending_login_mtx_;
		std::unordered_map<std::uint32_t, LoginSessionAuthState> login_sessions_;
		std::unordered_map<std::uint64_t, std::uint32_t> account_session_index_;
		std::unordered_map<std::uint64_t, std::uint32_t> char_session_index_;
		std::unordered_map<std::string, std::uint32_t> login_session_index_;
		std::unordered_map<std::string, std::uint32_t> world_token_index_;

		// login client 소켓이 먼저 끊겨도 world_enter_success_notify가 늦게 도착할 수 있다.
		// 이 경우 login_session/world_token 기준으로 후속 notify를 소비할 수 있게
		// detached 상태를 잠시 보관한다.
		std::unordered_map<std::string, LoginSessionAuthState> detached_world_enter_by_token_;
		std::unordered_map<std::string, std::string> detached_world_enter_token_by_login_session_;
		std::unordered_map<std::uint64_t, PendingLoginRequest> pending_login_requests_;

		HostedLineEntry client_line_{};
		OutboundLineEntry world_line_{};
		OutboundLineEntry account_line_{};

		std::shared_ptr<LoginHandler> login_handler_;
		std::shared_ptr<LoginAccountHandler> account_handler_;

		std::atomic<bool> account_ready_{ false };
		std::atomic<std::uint32_t> account_sid_{ 0 };
		std::atomic<std::uint32_t> account_serial_{ 0 };
		std::atomic<std::uint32_t> account_server_id_{ 0 };

		std::atomic<std::uint64_t> next_login_request_id_{ 1 };
	};

} // namespace dc
