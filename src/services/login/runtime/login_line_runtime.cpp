#include "services/login/runtime/login_line_runtime.h"

#include <cstdio>
#include <ctime>
#include <memory>
#include <random>
#include <sstream>

#include <spdlog/spdlog.h>

#include "proto/client/login_proto.h"
#include "proto/common/packet_util.h"
#include "server_common/runtime/line_client_start_helper.h"
#include "server_common/runtime/line_start_helper.h"

namespace dc {

    namespace {
        std::string ToHexToken_(std::uint64_t a, std::uint64_t b)
        {
            std::ostringstream oss;
            oss << std::hex << a << b;
            auto s = oss.str();
            if (s.size() > 32) s.resize(32);
            if (s.size() < 32) s.append(32 - s.size(), '0');
            return s;
        }
    }

    LoginLineRuntime::LoginLineRuntime(std::uint16_t port, std::string world_host, std::uint16_t world_port)
        : port_(port)
        , world_host_(std::move(world_host))
        , world_port_(world_port)
    {
    }

    bool LoginLineRuntime::IsWorldReady() const noexcept
    {
        return world_ready_.load(std::memory_order_acquire);
    }

    void LoginLineRuntime::MarkWorldRegistered(
        std::uint32_t sid,
        std::uint32_t serial,
        std::uint32_t server_id,
        std::string_view server_name,
        std::uint16_t listen_port)
    {
        world_sid_.store(sid, std::memory_order_relaxed);
        world_serial_.store(serial, std::memory_order_relaxed);
        world_server_id_.store(server_id, std::memory_order_relaxed);
        world_ready_.store(true, std::memory_order_release);

        spdlog::info(
            "LoginLineRuntime world ready. sid={} serial={} server_id={} server_name={} listen_port={}",
            sid, serial, server_id, server_name, listen_port);
    }

    void LoginLineRuntime::MarkWorldDisconnected(
        std::uint32_t sid,
        std::uint32_t serial)
    {
        const auto cur_sid = world_sid_.load(std::memory_order_relaxed);
        const auto cur_serial = world_serial_.load(std::memory_order_relaxed);

        if (cur_sid != 0 && (cur_sid != sid || cur_serial != serial)) {
            spdlog::debug(
                "LoginLineRuntime ignore stale world disconnect. sid={} serial={} current_sid={} current_serial={}",
                sid, serial, cur_sid, cur_serial);
            return;
        }

        world_ready_.store(false, std::memory_order_release);
        world_sid_.store(0, std::memory_order_relaxed);
        world_serial_.store(0, std::memory_order_relaxed);
        world_server_id_.store(0, std::memory_order_relaxed);

        spdlog::warn("LoginLineRuntime world not ready. sid={} serial={}", sid, serial);
    }

    std::string LoginLineRuntime::GenerateWorldToken_() const
    {
        static thread_local std::mt19937_64 rng{ std::random_device{}() };
        return ToHexToken_(rng(), rng());
    }

    std::uint64_t LoginLineRuntime::ResolveAccountId_(std::string_view login_id) const
    {
        std::uint64_t h = 1469598103934665603ull;
        for (char c : login_id) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        return h ? h : 1;
    }

    std::uint64_t LoginLineRuntime::ResolveCharId_(std::uint64_t selected_char_id, std::uint64_t account_id) const
    {
        if (selected_char_id != 0) {
            return selected_char_id;
        }
        return (account_id << 8) | 1ull;
    }

    void LoginLineRuntime::RemoveLoginSession_NoLock_(std::uint32_t sid, std::uint32_t serial)
    {
        auto it = login_sessions_.find(sid);
        if (it == login_sessions_.end()) {
            return;
        }

        if (it->second.serial != serial) {
            return;
        }

        if (it->second.account_id != 0) {
            auto ia = account_session_index_.find(it->second.account_id);
            if (ia != account_session_index_.end() && ia->second == sid) {
                account_session_index_.erase(ia);
            }
        }

        if (it->second.char_id != 0) {
            auto ic = char_session_index_.find(it->second.char_id);
            if (ic != char_session_index_.end() && ic->second == sid) {
                char_session_index_.erase(ic);
            }
        }

        login_sessions_.erase(it);
    }

    void LoginLineRuntime::AddDuplicateCandidateBySid_NoLock_(
        std::uint32_t sid,
        std::uint32_t new_sid,
        std::uint32_t new_serial,
        std::vector<DuplicateSessionRef>& out)
    {
        auto it = login_sessions_.find(sid);
        if (it == login_sessions_.end()) {
            return;
        }

        const auto& st = it->second;

        if (st.sid == new_sid && st.serial == new_serial) {
            return;
        }

        for (const auto& e : out) {
            if (e.sid == st.sid && e.serial == st.serial) {
                return;
            }
        }

        out.push_back(DuplicateSessionRef{
            st.sid,
            st.serial,
            st.account_id,
            st.char_id
            });
    }

    std::vector<LoginLineRuntime::DuplicateSessionRef>
        LoginLineRuntime::CollectDuplicateSessions_NoLock_(
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::uint32_t new_sid,
            std::uint32_t new_serial)
    {
        std::vector<DuplicateSessionRef> victims;

        if (account_id != 0) {
            auto ia = account_session_index_.find(account_id);
            if (ia != account_session_index_.end()) {
                AddDuplicateCandidateBySid_NoLock_(ia->second, new_sid, new_serial, victims);
            }
        }

        if (char_id != 0) {
            auto ic = char_session_index_.find(char_id);
            if (ic != char_session_index_.end()) {
                AddDuplicateCandidateBySid_NoLock_(ic->second, new_sid, new_serial, victims);
            }
        }

        return victims;
    }

    void LoginLineRuntime::CloseDuplicateLoginSessions_(const std::vector<DuplicateSessionRef>& victims)
    {
        auto* server = client_line_.host.server();
        if (!server) {
            spdlog::warn("CloseDuplicateLoginSessions_: login client server is null");
            return;
        }

        for (const auto& v : victims) {
            spdlog::warn(
                "Duplicate login detected. closing old session sid={} serial={} account_id={} char_id={}",
                v.sid, v.serial, v.account_id, v.char_id);

            server->close(v.sid, v.serial);
        }
    }

    bool LoginLineRuntime::IssueLoginSuccess(
        std::uint32_t sid,
        std::uint32_t serial,
        std::string_view login_id,
        std::uint64_t selected_char_id)
    {
        if (!IsWorldReady()) {
            spdlog::warn("IssueLoginSuccess blocked: world not ready sid={}", sid);
            return false;
        }

        const auto account_id = ResolveAccountId_(login_id);
        const auto char_id = ResolveCharId_(selected_char_id, account_id);
        const auto token = GenerateWorldToken_();

        std::vector<DuplicateSessionRef> victims;
        {
            std::lock_guard lk(login_sessions_mtx_);
            victims = CollectDuplicateSessions_NoLock_(account_id, char_id, sid, serial);

            // 기존 세션 인덱스는 미리 제거해 둔다.
            // 실제 close callback이 나중에 와도 stale serial이면 안전하게 무시된다.
            for (const auto& v : victims) {
                RemoveLoginSession_NoLock_(v.sid, v.serial);
            }
        }

        CloseDuplicateLoginSessions_(victims);

        if (!world_handler_) {
            spdlog::error("IssueLoginSuccess failed: world_handler_ is null");
            return false;
        }

        const auto world_sid = world_sid_.load(std::memory_order_relaxed);
        const auto world_serial = world_serial_.load(std::memory_order_relaxed);

        if (!world_handler_->SendAuthTicketUpsert(
            1,
            world_sid,
            world_serial,
            account_id,
            char_id,
            token,
            static_cast<std::uint64_t>(std::time(nullptr) + 30)))
        {
            spdlog::error("IssueLoginSuccess failed: SendAuthTicketUpsert sid={} char_id={}", sid, char_id);
            return false;
        }

        {
            std::lock_guard lk(login_sessions_mtx_);

            // 같은 sid 슬롯이 재사용된 경우를 대비해서 한번 더 정리
            RemoveLoginSession_NoLock_(sid, serial);

            auto& st = login_sessions_[sid];
            st.sid = sid;
            st.serial = serial;
            st.logged_in = true;
            st.account_id = account_id;
            st.char_id = char_id;
            st.world_token = token;
            st.issued_at = std::chrono::steady_clock::now();
            st.expires_at = st.issued_at + std::chrono::seconds(30);

            account_session_index_[account_id] = sid;
            char_session_index_[char_id] = sid;
        }

        proto::S2C_login_result res{};
        res.ok = 1;
        res.account_id = account_id;
        res.char_id = char_id;
        res.world_port = world_port_;
        std::snprintf(res.world_host, sizeof(res.world_host), "%s", world_host_.c_str());
        std::snprintf(res.world_token, sizeof(res.world_token), "%s", token.c_str());

        const auto h = proto::make_header(
            static_cast<std::uint16_t>(proto::LoginS2CMsg::login_result),
            static_cast<std::uint16_t>(sizeof(res)));

        if (!login_handler_) {
            spdlog::error("IssueLoginSuccess failed: login_handler_ is null");
            return false;
        }

        if (!login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res))) {
            spdlog::error("IssueLoginSuccess failed: send login_result sid={}", sid);
            return false;
        }

        spdlog::info(
            "IssueLoginSuccess sid={} serial={} account_id={} char_id={} token={}",
            sid, serial, account_id, char_id, token);

        return true;
    }

    void LoginLineRuntime::RemoveLoginSession(std::uint32_t sid, std::uint32_t serial)
    {
        std::lock_guard lk(login_sessions_mtx_);
        RemoveLoginSession_NoLock_(sid, serial);
    }

    bool LoginLineRuntime::OnRuntimeInit()
    {
        dc::InitHostedLineEntry(
            client_line_,
            0,
            "login-client",
            port_,
            false,
            0);

        login_handler_ = std::make_shared<LoginHandler>(*this);

        if (!dc::StartHostedLine(
            client_line_,
            io_,
            login_handler_,
            [](std::uint64_t, std::function<void()> fn) {
            if (fn) fn();
        }))
        {
            spdlog::error("LoginLineRuntime failed to start hosted line. port={}", port_);
            return false;
        }

        world_handler_ = std::make_shared<LoginWorldHandler>(
            [this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port) {
            MarkWorldRegistered(sid, serial, server_id, server_name, listen_port);
        },
            [this](std::uint32_t sid, std::uint32_t serial) {
            MarkWorldDisconnected(sid, serial);
        });

        world_handler_->SetServerIdentity(
            1,
            "login",
            port_);

        dc::InitOutboundLineEntry(
            world_line_,
            1,
            "login-world",
            world_host_,
            world_port_,
            true,
            1000,
            10000);

        if (!dc::StartOutboundLine(
            world_line_,
            io_,
            world_handler_,
            [](std::uint64_t, std::function<void()> fn) {
            if (fn) fn();
        }))
        {
            spdlog::error(
                "LoginLineRuntime failed to start outbound world line. remote={}:{}",
                world_host_,
                world_port_);
            return false;
        }

        spdlog::info(
            "LoginLineRuntime started. client_port={} world_remote={}:{}",
            port_,
            world_host_,
            world_port_);
        return true;
    }

    void LoginLineRuntime::OnBeforeIoStop()
    {
        world_ready_.store(false, std::memory_order_release);
    }

    void LoginLineRuntime::OnAfterIoStop()
    {
        world_line_.host.Stop();
        client_line_.host.Stop();

        {
            std::lock_guard lk(login_sessions_mtx_);
            login_sessions_.clear();
            account_session_index_.clear();
            char_session_index_.clear();
        }

        world_handler_.reset();
        login_handler_.reset();

        world_ready_.store(false, std::memory_order_release);
        world_sid_.store(0, std::memory_order_relaxed);
        world_serial_.store(0, std::memory_order_relaxed);
        world_server_id_.store(0, std::memory_order_relaxed);

        spdlog::info("LoginLineRuntime stopped.");
    }

    void LoginLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point)
    {
    }

} // namespace dc
