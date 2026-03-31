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
#include "proto/client/login_proto.h"
#include "proto/internal/login_account_proto.h"
#include "services/login/handler/login_handler.h"
#include "services/login/handler/login_account_handler.h"
#include "services/runtime/login_auth_types.h"
#include "services/runtime/server_runtime_base.h"

namespace dc {

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
            bool use_protobuf);

        bool IssueWorldListRequest(
            std::uint32_t sid,
            std::uint32_t serial,
            bool use_protobuf);

        bool IssueWorldSelectRequest(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint16_t world_id,
            std::uint16_t channel_id,
            bool use_protobuf);

        bool IssueCharacterListRequest(
            std::uint32_t sid,
            std::uint32_t serial,
            bool use_protobuf);

        bool IssueCharacterSelectRequest(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint64_t char_id,
            bool use_protobuf);

        void RemoveLoginSession(std::uint32_t sid, std::uint32_t serial);

        void OnAccountRegistered(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint32_t server_id,
            std::string_view server_name,
            std::uint16_t listen_port);

        void OnAccountDisconnected(
            std::uint32_t sid,
            std::uint32_t serial);

        void OnAccountAuthResult(
            std::uint64_t trace_id,
            std::uint64_t request_id,
            bool ok,
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view login_session,
            std::string_view world_token,
            std::string_view world_host,
            std::uint16_t world_port,
            std::string_view fail_reason);

        void OnWorldListResult(
            std::uint64_t trace_id,
            std::uint64_t request_id,
            bool ok,
            std::uint64_t account_id,
            std::uint16_t count,
            const ::proto::internal::login_account::WorldSummary* worlds,
            std::string_view fail_reason);

        void OnWorldSelectResult(
            std::uint64_t trace_id,
            std::uint64_t request_id,
            bool ok,
            std::uint64_t account_id,
            std::uint16_t world_id,
            std::string_view login_session,
            std::string_view world_host,
            std::uint16_t world_port,
            std::string_view fail_reason);

        void OnCharacterListResult(
            std::uint64_t trace_id,
            std::uint64_t request_id,
            bool ok,
            std::uint64_t account_id,
            std::uint16_t count,
            const ::proto::internal::login_account::CharacterSummary* characters,
            std::string_view fail_reason);

        void OnCharacterSelectResult(
            std::uint64_t trace_id,
            std::uint64_t request_id,
            bool ok,
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view login_session,
            std::string_view world_token,
            std::string_view world_host,
            std::uint16_t world_port,
            std::string_view fail_reason);

        void OnWorldEnterSuccessNotify(
            std::uint64_t trace_id,
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view login_session,
            std::string_view world_token);

    private:
        struct DuplicateSessionRef
        {
            std::uint32_t sid = 0;
            std::uint32_t serial = 0;
            std::uint64_t account_id = 0;
            std::uint64_t char_id = 0;
        };

        struct SessionRef
        {
            std::uint32_t sid = 0;
            std::uint32_t serial = 0;

            [[nodiscard]] bool valid() const noexcept
            {
                return sid != 0 && serial != 0;
            }
        };

        struct PendingLoginRequest
        {
            std::uint64_t trace_id = 0;
            std::uint64_t request_id = 0;
            std::uint32_t client_sid = 0;
            std::uint32_t client_serial = 0;
            bool use_protobuf = false;
            std::string login_id;
            std::string password;
            std::chrono::steady_clock::time_point issued_at{};
        };

        struct PendingWorldListRequest
        {
            std::uint64_t trace_id = 0;
            std::uint64_t request_id = 0;
            std::uint32_t client_sid = 0;
            std::uint32_t client_serial = 0;
            bool use_protobuf = false;
            std::uint64_t account_id = 0;
            std::string login_session;
            std::chrono::steady_clock::time_point issued_at{};
        };

        struct PendingWorldSelectRequest
        {
            std::uint64_t trace_id = 0;
            std::uint64_t request_id = 0;
            std::uint32_t client_sid = 0;
            std::uint32_t client_serial = 0;
            bool use_protobuf = false;
            std::uint64_t account_id = 0;
            std::uint16_t world_id = 0;
            std::uint16_t channel_id = 0;
            std::string login_session;
            std::chrono::steady_clock::time_point issued_at{};
        };

        struct PendingCharacterListRequest
        {
            std::uint64_t trace_id = 0;
            std::uint64_t request_id = 0;
            std::uint32_t client_sid = 0;
            std::uint32_t client_serial = 0;
            bool use_protobuf = false;
            std::uint64_t account_id = 0;
            std::uint16_t world_id = 0;
            std::string login_session;
            std::chrono::steady_clock::time_point issued_at{};
        };

        struct PendingCharacterSelectRequest
        {
            std::uint64_t trace_id = 0;
            std::uint64_t request_id = 0;
            std::uint32_t client_sid = 0;
            std::uint32_t client_serial = 0;
            bool use_protobuf = false;
            std::uint64_t account_id = 0;
            std::uint64_t char_id = 0;
            std::string login_session;
            std::chrono::steady_clock::time_point issued_at{};
        };

    private:
        bool IsAccountReady() const noexcept;
        bool SendAccountAuthRequest_(const PendingLoginRequest& pending);
        bool SendWorldListRequest_(const PendingWorldListRequest& pending);
        bool SendWorldSelectRequest_(const PendingWorldSelectRequest& pending);
        bool SendCharacterListRequest_(const PendingCharacterListRequest& pending);
        bool SendCharacterSelectRequest_(const PendingCharacterSelectRequest& pending);

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
            std::uint16_t world_port,
            bool use_protobuf);

        bool SendWorldListResult_(
            std::uint32_t sid,
            std::uint32_t serial,
            bool ok,
            std::uint16_t count,
            const ::proto::internal::login_account::WorldSummary* worlds,
            std::string_view fail_reason,
            bool use_protobuf);

        bool SendWorldSelectResult_(
            std::uint32_t sid,
            std::uint32_t serial,
            bool ok,
            std::uint16_t world_id,
            std::string_view world_host,
            std::uint16_t world_port,
            ::proto::login::WorldSelectFailReason fail_reason,
            bool use_protobuf);

        bool SendCharacterListResult_(
            std::uint32_t sid,
            std::uint32_t serial,
            bool ok,
            std::uint16_t count,
            const ::proto::internal::login_account::CharacterSummary* characters,
            std::string_view fail_reason,
            bool use_protobuf);

        bool SendCharacterSelectResult_(
            std::uint32_t sid,
            std::uint32_t serial,
            bool ok,
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view world_token,
            std::string_view world_host,
            std::uint16_t world_port,
            ::proto::login::CharacterSelectFailReason fail_reason,
            bool use_protobuf);

        void ExpirePendingLoginRequests_(std::chrono::steady_clock::time_point now);

        bool SendLoginResultFail_(
            std::uint32_t sid,
            std::uint32_t serial,
            const char* reason,
            bool use_protobuf);

    private:
        bool OnRuntimeInit() override;
        bool LoadIniFile_();
        void OnBeforeIoStop() override;
        void OnAfterIoStop() override;
        void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

        void RemoveLoginSession_NoLock_(std::uint32_t sid, std::uint32_t serial);
        void EraseDetachedWorldEnterState_NoLock_(
            std::string_view login_session,
            std::string_view world_token);
        void AddDuplicateCandidateBySid_NoLock_(
            const SessionRef& ref,
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
        std::unordered_map<std::uint64_t, SessionRef> account_session_index_;
        std::unordered_map<std::uint64_t, SessionRef> char_session_index_;
        std::unordered_map<std::string, SessionRef> login_session_index_;
        std::unordered_map<std::string, SessionRef> world_token_index_;
        std::unordered_map<std::string, LoginSessionAuthState> detached_world_enter_by_token_;
        std::unordered_map<std::string, std::string> detached_world_enter_token_by_login_session_;
        std::unordered_map<std::uint64_t, PendingLoginRequest> pending_login_requests_;
        std::unordered_map<std::uint64_t, PendingWorldListRequest> pending_world_list_requests_;
        std::unordered_map<std::uint64_t, PendingWorldSelectRequest> pending_world_select_requests_;
        std::unordered_map<std::uint64_t, PendingCharacterListRequest> pending_character_list_requests_;
        std::unordered_map<std::uint64_t, PendingCharacterSelectRequest> pending_character_select_requests_;

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
        std::atomic<std::uint64_t> next_enter_trace_id_{ 1 };
    };

} // namespace dc





