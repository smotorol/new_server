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

namespace pt_l = proto::login;

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

	LoginLineRuntime::LoginLineRuntime(
		std::uint16_t port,
		std::string world_host,
		std::uint16_t world_port,
		std::string account_host,
		std::uint16_t account_port)
		: port_(port)
		, world_host_(std::move(world_host))
		, world_port_(world_port)
		, account_host_(std::move(account_host))
		, account_port_(account_port)
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
			"LoginLineRuntime world line ready. sid={} serial={} server_id={} server_name={} listen_port={}",
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

		std::vector<PendingWorldTicketUpsert> failed;
		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			for (auto& [token, item] : pending_world_ticket_upserts_) {
				failed.push_back(std::move(item));
			}
			pending_world_ticket_upserts_.clear();
		}

		for (const auto& item : failed) {
			SendLoginResultFail_(item.pending.client_sid, item.pending.client_serial, "world_disconnected");
		}

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

	bool LoginLineRuntime::IssueLoginRequest(
		std::uint32_t sid,
		std::uint32_t serial,
		std::string_view login_id,
		std::string_view password,
		std::uint64_t selected_char_id)
	{
		if (!IsWorldReady()) {
			spdlog::warn("IssueLoginRequest blocked: world not ready sid={}", sid);
			return false;
		}

		if (!IsAccountReady()) {
			spdlog::warn("IssueLoginRequest blocked: account not ready sid={}", sid);
			return false;
		}

		PendingLoginRequest pending{};
		pending.request_id = next_login_request_id_.fetch_add(1, std::memory_order_relaxed);
		pending.client_sid = sid;
		pending.client_serial = serial;
		pending.login_id.assign(login_id.data(), login_id.size());
		pending.password.assign(password.data(), password.size());
		pending.selected_char_id = selected_char_id;
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_login_requests_[pending.request_id] = pending;
		}

		if (!SendAccountAuthRequest_(pending)) {
			std::lock_guard lk(pending_login_mtx_);
			pending_login_requests_.erase(pending.request_id);
			return false;
		}

		spdlog::info(
			"IssueLoginRequest forwarded request_id={} sid={} serial={} login_id={}",
			pending.request_id,
			sid,
			serial,
			login_id);

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
		},
			[this](bool accepted, std::uint64_t account_id, std::uint64_t char_id, std::string_view world_token) {
			OnWorldAuthTicketUpsertAck(accepted, account_id, char_id, world_token);
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

		account_handler_ = std::make_shared<LoginAccountHandler>(
			[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port) {
			MarkAccountRegistered(sid, serial, server_id, server_name, listen_port);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			MarkAccountDisconnected(sid, serial);
		},
			[this](std::uint64_t request_id,
				bool ok,
				std::uint64_t account_id,
				std::uint64_t char_id,
				std::string_view login_session,
				std::string_view world_host,
				std::uint16_t world_port,
				std::string_view fail_reason) {
			OnAccountAuthResult(
				request_id,
				ok,
				account_id,
				char_id,
				login_session,
				world_host,
				world_port,
				fail_reason);
		});

		account_handler_->SetServerIdentity(
			1,
			"login",
			port_);

		dc::InitOutboundLineEntry(
			account_line_,
			2,
			"login-account",
			account_host_,
			account_port_,
			true,
			1000,
			10000);

		if (!dc::StartOutboundLine(
			account_line_,
			io_,
			account_handler_,
			[](std::uint64_t, std::function<void()> fn) {
			if (fn) fn();
		}))
		{
			spdlog::error(
				"LoginLineRuntime failed to start outbound account line. remote={}:{}",
				account_host_,
				account_port_);
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
		account_ready_.store(false, std::memory_order_release);
	}

	void LoginLineRuntime::OnAfterIoStop()
	{
		account_line_.host.Stop();
		world_line_.host.Stop();
		client_line_.host.Stop();

		{
			std::lock_guard lk(login_sessions_mtx_);
			login_sessions_.clear();
			account_session_index_.clear();
			char_session_index_.clear();
		}

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_login_requests_.clear();
		}

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			pending_world_ticket_upserts_.clear();
		}

		account_handler_.reset();
		world_handler_.reset();
		login_handler_.reset();

		world_ready_.store(false, std::memory_order_release);
		world_sid_.store(0, std::memory_order_relaxed);
		world_serial_.store(0, std::memory_order_relaxed);
		world_server_id_.store(0, std::memory_order_relaxed);

		account_ready_.store(false, std::memory_order_release);
		account_sid_.store(0, std::memory_order_relaxed);
		account_serial_.store(0, std::memory_order_relaxed);
		account_server_id_.store(0, std::memory_order_relaxed);
	}

	bool LoginLineRuntime::IsAccountReady() const noexcept
	{
		return account_ready_.load(std::memory_order_acquire);
	}

	void LoginLineRuntime::MarkAccountRegistered(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::string_view server_name,
		std::uint16_t listen_port)
	{
		account_sid_.store(sid, std::memory_order_relaxed);
		account_serial_.store(serial, std::memory_order_relaxed);
		account_server_id_.store(server_id, std::memory_order_relaxed);
		account_ready_.store(true, std::memory_order_release);

		spdlog::info(
			"LoginLineRuntime account ready sid={} serial={} server_id={} server_name={} listen_port={}",
			sid, serial, server_id, server_name, listen_port);
	}

	void LoginLineRuntime::MarkAccountDisconnected(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		const auto cur_sid = account_sid_.load(std::memory_order_relaxed);
		const auto cur_serial = account_serial_.load(std::memory_order_relaxed);

		if (cur_sid != 0 && (cur_sid != sid || cur_serial != serial)) {
			return;
		}

		account_ready_.store(false, std::memory_order_release);
		account_sid_.store(0, std::memory_order_relaxed);
		account_serial_.store(0, std::memory_order_relaxed);
		account_server_id_.store(0, std::memory_order_relaxed);
	}

	bool LoginLineRuntime::SendAccountAuthRequest_(const PendingLoginRequest& pending)
	{
		if (!account_handler_) {
			return false;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);

		return account_handler_->SendAccountAuthRequest(
			2,
			sid,
			serial,
			pending.request_id,
			pending.login_id,
			pending.password,
			pending.selected_char_id);
	}

	void LoginLineRuntime::OnAccountAuthResult(
		std::uint64_t request_id,
		bool ok,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_host,
		std::uint16_t world_port,
		std::string_view fail_reason)
	{
		PendingLoginRequest pending{};

		{
			std::lock_guard lk(pending_login_mtx_);
			const auto it = pending_login_requests_.find(request_id);
			if (it == pending_login_requests_.end()) {
				spdlog::warn("OnAccountAuthResult dropped: request_id={} not found", request_id);
				return;
			}

			pending = std::move(it->second);
			pending_login_requests_.erase(it);
		}

		pending.world_host.assign(world_host.data(), world_host.size());
		pending.world_port = world_port;

		CompleteLoginRequest_(std::move(pending), ok, account_id, char_id, login_session, fail_reason);
	}

	void LoginLineRuntime::CompleteLoginRequest_(
		PendingLoginRequest pending,
		bool ok,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view fail_reason)
	{
		if (!ok) {
			SendLoginResultFail_(
				pending.client_sid,
				pending.client_serial,
				fail_reason.empty() ? "account_auth_failed" : fail_reason.data());
			return;
		}

		if (pending.world_host.empty() || pending.world_port == 0) {
			SendLoginResultFail_(pending.client_sid, pending.client_serial, "invalid_world_endpoint");
			return;
		}

		if (!world_ready_.load(std::memory_order_acquire)) {
			SendLoginResultFail_(pending.client_sid, pending.client_serial, "world_not_ready");
			return;
		}

		const auto token = GenerateWorldToken_();
		const auto expire_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);

		{
			std::lock_guard lk(pending_world_upsert_mtx_);

			PendingWorldTicketUpsert item{};
			item.pending = std::move(pending);
			item.account_id = account_id;
			item.char_id = char_id;
			item.login_session.assign(login_session.data(), login_session.size());
			item.world_token = token;
			item.issued_at = std::chrono::steady_clock::now();

			pending_world_ticket_upserts_[item.world_token] = std::move(item);
		}

		if (!world_handler_ ||
			!world_handler_->SendAuthTicketUpsert(
				1,
				world_sid_.load(std::memory_order_relaxed),
				world_serial_.load(std::memory_order_relaxed),
				account_id,
				char_id,
				login_session,
				token,
				static_cast<std::uint64_t>(std::time(nullptr) + 30)))
		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			pending_world_ticket_upserts_.erase(token);

			SendLoginResultFail_(
				pending.client_sid,
				pending.client_serial,
				"world_ticket_upsert_failed");
			return;
		}

		spdlog::info(
			"LoginLineRuntime waiting world upsert ack. account_id={} char_id={} token={}",
			account_id,
			char_id,
			token);
	}

	void LoginLineRuntime::ExpirePendingLoginRequests_(std::chrono::steady_clock::time_point now)
	{
		std::vector<PendingLoginRequest> expired;

		{
			std::lock_guard lk(pending_login_mtx_);
			for (auto it = pending_login_requests_.begin(); it != pending_login_requests_.end();) {
				if (now - it->second.issued_at >= std::chrono::seconds(5)) {
					expired.push_back(std::move(it->second));
					it = pending_login_requests_.erase(it);
				}
				else {
					++it;
				}
			}
		}

		for (const auto& e : expired) {
			SendLoginResultFail_(e.client_sid, e.client_serial, "account_timeout");
		}
	}

	bool LoginLineRuntime::SendLoginResultFail_(
		std::uint32_t sid,
		std::uint32_t serial,
		const char* reason)
	{
		(void)reason;

		if (!login_handler_) {
			return false;
		}

		pt_l::S2C_login_result res{};
		res.ok = 0;

		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result),
			static_cast<std::uint16_t>(sizeof(res)));

		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

	bool LoginLineRuntime::SendLoginResultSuccess_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view token,
		std::string_view world_host,
		std::uint16_t world_port)
	{
		if (!login_handler_) {
			return false;
		}

		pt_l::S2C_login_result res{};
		res.ok = 1;
		res.account_id = account_id;
		res.char_id = char_id;
		res.world_port = world_port;

		std::snprintf(
			res.login_session,
			sizeof(res.login_session),
			"%.*s",
			static_cast<int>(login_session.size()),
			login_session.data());

		std::snprintf(
			res.world_host,
			sizeof(res.world_host),
			"%.*s",
			static_cast<int>(world_host.size()),
			world_host.data());

		std::snprintf(
			res.world_token,
			sizeof(res.world_token),
			"%.*s",
			static_cast<int>(token.size()),
			token.data());

		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result),
			static_cast<std::uint16_t>(sizeof(res)));

		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

	void LoginLineRuntime::OnWorldAuthTicketUpsertAck(
		bool accepted,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view world_token)
	{
		PendingWorldTicketUpsert item{};

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			const auto it = pending_world_ticket_upserts_.find(std::string(world_token));
			if (it == pending_world_ticket_upserts_.end()) {
				spdlog::warn(
					"OnWorldAuthTicketUpsertAck dropped: token={} not found",
					world_token);
				return;
			}

			item = std::move(it->second);
			pending_world_ticket_upserts_.erase(it);
		}

		if (!accepted) {
			SendLoginResultFail_(
				item.pending.client_sid,
				item.pending.client_serial,
				"world_ticket_rejected");
			return;
		}

		if (item.account_id != account_id || item.char_id != char_id) {
			spdlog::warn(
				"OnWorldAuthTicketUpsertAck mismatch: token={} ack_account_id={} ack_char_id={} stored_account_id={} stored_char_id={}",
				world_token,
				account_id,
				char_id,
				item.account_id,
				item.char_id);

			SendLoginResultFail_(
				item.pending.client_sid,
				item.pending.client_serial,
				"world_ticket_ack_mismatch");
			return;
		}

		{
			std::lock_guard lk(login_sessions_mtx_);

			RemoveLoginSession_NoLock_(item.pending.client_sid, item.pending.client_serial);

			auto& st = login_sessions_[item.pending.client_sid];
			st.sid = item.pending.client_sid;
			st.serial = item.pending.client_serial;
			st.logged_in = true;
			st.account_id = item.account_id;
			st.char_id = item.char_id;
			st.login_session = item.login_session;
			st.world_token = item.world_token;
			st.issued_at = std::chrono::steady_clock::now();
			st.expires_at = st.issued_at + std::chrono::seconds(30);

			account_session_index_[item.account_id] = item.pending.client_sid;
			char_session_index_[item.char_id] = item.pending.client_sid;
		}

		SendLoginResultSuccess_(
			item.pending.client_sid,
			item.pending.client_serial,
			item.account_id,
			item.char_id,
			item.login_session,
			item.world_token,
			item.pending.world_host,
			item.pending.world_port);
	}

	void LoginLineRuntime::ExpirePendingWorldTicketUpserts_(std::chrono::steady_clock::time_point now)
	{
		std::vector<PendingWorldTicketUpsert> expired;

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			for (auto it = pending_world_ticket_upserts_.begin(); it != pending_world_ticket_upserts_.end();) {
				if (now - it->second.issued_at >= std::chrono::seconds(5)) {
					expired.push_back(std::move(it->second));
					it = pending_world_ticket_upserts_.erase(it);
				}
				else {
					++it;
				}
			}
		}

		for (const auto& item : expired) {
			SendLoginResultFail_(
				item.pending.client_sid,
				item.pending.client_serial,
				"world_ticket_ack_timeout");
		}
	}

	void LoginLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now)
	{
		ExpirePendingLoginRequests_(now);
		ExpirePendingWorldTicketUpserts_(now);
	}

} // namespace dc
