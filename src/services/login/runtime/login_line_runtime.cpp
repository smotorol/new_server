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

        if (!world_handler_) {
            spdlog::error("IssueLoginSuccess failed: login-world handler cast failed");
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
            auto& st = login_sessions_[sid];
            st.sid = sid;
            st.serial = serial;
            st.logged_in = true;
            st.account_id = account_id;
            st.char_id = char_id;
            st.world_token = token;
            st.issued_at = std::chrono::steady_clock::now();
            st.expires_at = st.issued_at + std::chrono::seconds(30);
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
            spdlog::error("IssueLoginSuccess failed: login handler cast failed");
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
        auto it = login_sessions_.find(sid);
        if (it == login_sessions_.end()) {
            return;
        }
        if (it->second.serial != serial) {
            return;
        }
        login_sessions_.erase(it);
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
