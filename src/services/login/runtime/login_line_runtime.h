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
#include "services/login/handler/login_world_handler.h"
#include "services/runtime/login_auth_types.h"
#include "services/runtime/server_runtime_base.h"

namespace dc {

    class LoginLineRuntime final : public ServerRuntimeBase {
    public:
        LoginLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port);
        ~LoginLineRuntime() override = default;

        bool IsWorldReady() const noexcept;

        bool IssueLoginSuccess(
            std::uint32_t sid,
            std::uint32_t serial,
            std::string_view login_id,
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

    private:
        bool OnRuntimeInit() override;
        void OnBeforeIoStop() override;
        void OnAfterIoStop() override;
        void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

        void MarkWorldRegistered(
            std::uint32_t sid,
            std::uint32_t serial,
            std::uint32_t server_id,
            std::string_view server_name,
            std::uint16_t listen_port);

        void MarkWorldDisconnected(
            std::uint32_t sid,
            std::uint32_t serial);

        std::string GenerateWorldToken_() const;
        std::uint64_t ResolveAccountId_(std::string_view login_id) const;
        std::uint64_t ResolveCharId_(std::uint64_t selected_char_id, std::uint64_t account_id) const;

        void RemoveLoginSession_NoLock_(std::uint32_t sid, std::uint32_t serial);
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
        std::string world_host_ = "127.0.0.1";
        std::uint16_t world_port_ = 0;

        std::atomic<bool> world_ready_{ false };
        std::atomic<std::uint32_t> world_sid_{ 0 };
        std::atomic<std::uint32_t> world_serial_{ 0 };
        std::atomic<std::uint32_t> world_server_id_{ 0 };

        std::mutex login_sessions_mtx_;
        std::unordered_map<std::uint32_t, LoginSessionAuthState> login_sessions_;
        std::unordered_map<std::uint64_t, std::uint32_t> account_session_index_;
        std::unordered_map<std::uint64_t, std::uint32_t> char_session_index_;

        HostedLineEntry client_line_{};
        OutboundLineEntry world_line_{};

        std::shared_ptr<LoginHandler> login_handler_;
        std::shared_ptr<LoginWorldHandler> world_handler_;
    };

} // namespace dc
