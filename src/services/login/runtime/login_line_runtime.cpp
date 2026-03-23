#include "services/login/runtime/login_line_runtime.h"

#include <cstdio>
#include <ctime>
#include <memory>

#include <spdlog/spdlog.h>

#include "proto/client/login_proto.h"
#include "proto/common/packet_util.h"
#include "server_common/runtime/line_client_start_helper.h"
#include "server_common/runtime/line_start_helper.h"
#include "server_common/session/session_key.h"
#include "server_common/log/flow_event_codes.h"

#include "shared/constants.h"

namespace pt_l = proto::login;

namespace dc {

	LoginLineRuntime::LoginLineRuntime(
		std::uint16_t port,
		std::string account_host,
		std::uint16_t account_port)
		: port_(port)
		, account_host_(std::move(account_host))
		, account_port_(account_port)
	{
	}

	bool LoginLineRuntime::IsValidAuthIdentity_(
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token) const noexcept
	{
		return account_id != 0 &&
			char_id != 0 &&
			!login_session.empty() &&
			!world_token.empty();
	}

	bool LoginLineRuntime::IsWorldReady() const noexcept
	{
		return true;
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
			if (ia != account_session_index_.end() &&
				dc::IsSameSessionKey(ia->second.sid, ia->second.serial, sid, serial)) {
				account_session_index_.erase(ia);
			}
		}

		if (it->second.char_id != 0) {
			auto ic = char_session_index_.find(it->second.char_id);
			if (ic != char_session_index_.end() &&
				dc::IsSameSessionKey(ic->second.sid, ic->second.serial, sid, serial)) {
				char_session_index_.erase(ic);
			}
		}

		const auto should_keep_detached_identity =
			it->second.logged_in &&
			IsValidAuthIdentity_(
				it->second.account_id,
				it->second.char_id,
				it->second.login_session,
				it->second.world_token);

		if (should_keep_detached_identity) {
			auto detached = it->second;
			detached.sid = 0;
			detached.serial = 0;
			detached_world_enter_by_token_[detached.world_token] = std::move(detached);
			detached_world_enter_token_by_login_session_[it->second.login_session] = it->second.world_token;
		}

		if (!it->second.login_session.empty()) {
			auto ils = login_session_index_.find(it->second.login_session);
			if (ils != login_session_index_.end() &&
				dc::IsSameSessionKey(ils->second.sid, ils->second.serial, sid, serial)) {
				login_session_index_.erase(ils);
			}
		}

		if (!it->second.world_token.empty()) {
			auto iwt = world_token_index_.find(it->second.world_token);
			if (iwt != world_token_index_.end() &&
				dc::IsSameSessionKey(iwt->second.sid, iwt->second.serial, sid, serial)) {
				world_token_index_.erase(iwt);
			}
		}

		login_sessions_.erase(it);
	}

	void LoginLineRuntime::EraseDetachedWorldEnterState_NoLock_(
		std::string_view login_session,
		std::string_view world_token)
	{
		if (!login_session.empty()) {
			auto ils = detached_world_enter_token_by_login_session_.find(std::string(login_session));
			if (ils != detached_world_enter_token_by_login_session_.end()) {
				detached_world_enter_token_by_login_session_.erase(ils);
			}
		}

		if (!world_token.empty()) {
			auto iwt = detached_world_enter_by_token_.find(std::string(world_token));
			if (iwt != detached_world_enter_by_token_.end()) {
				detached_world_enter_by_token_.erase(iwt);
			}
		}
	}

	void LoginLineRuntime::AddDuplicateCandidateBySid_NoLock_(
		const SessionRef& ref,
		std::uint32_t new_sid,
		std::uint32_t new_serial,
		std::vector<DuplicateSessionRef>& out)
	{
		if (!ref.valid()) {
			return;
		}

		const auto sid = ref.sid;
		auto it = login_sessions_.find(sid);
		if (it == login_sessions_.end()) {
			return;
		}

		const auto& st = it->second;
		if (st.serial != ref.serial) {
			return;
		}

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
			spdlog::warn("[{}] login client server is null", logevt::login::kDuplicateClose);
			return;
		}

		for (const auto& v : victims) {
			spdlog::warn(
				"[{}] closing old duplicate login session sid={} serial={} account_id={} char_id={}",
				logevt::login::kDuplicateClose,
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
		if (!IsAccountReady()) {
			spdlog::warn("[{}] blocked: account route not ready sid={}", logevt::login::kAuthReqDropped, sid);
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
			"[{}] forwarded request_id={} sid={} serial={} login_id={}",
			logevt::login::kAuthReqSent,
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
				std::string_view world_token,
				std::string_view world_host,
				std::uint16_t world_port,
				std::string_view fail_reason) {
			OnAccountAuthResult(
				request_id,
				ok,
				account_id,
				char_id,
				login_session,
				world_token,
				world_host,
				world_port,
				fail_reason);
		},
			[this](std::uint64_t account_id,
				std::uint64_t char_id,
				std::string_view login_session,
				std::string_view world_token) {
			OnWorldEnterSuccessNotify(account_id, char_id, login_session, world_token);
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
			"LoginLineRuntime started. client_port={} account_remote={}:{}",
			port_,
			account_host_,
			account_port_);
		return true;
	}

	void LoginLineRuntime::OnBeforeIoStop()
	{
		account_ready_.store(false, std::memory_order_release);
	}

	void LoginLineRuntime::OnAfterIoStop()
	{
		account_line_.host.Stop();
		client_line_.host.Stop();

		{
			std::lock_guard lk(login_sessions_mtx_);
			login_sessions_.clear();
			account_session_index_.clear();
			char_session_index_.clear();
			login_session_index_.clear();
			world_token_index_.clear();
			detached_world_enter_by_token_.clear();
			detached_world_enter_token_by_login_session_.clear();
		}

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_login_requests_.clear();
		}

		account_handler_.reset();
		login_handler_.reset();

		account_ready_.store(false, std::memory_order_release);
		account_sid_.store(0, std::memory_order_relaxed);
		account_serial_.store(0, std::memory_order_relaxed);
		account_server_id_.store(0, std::memory_order_relaxed);

		spdlog::info("[{}] login runtime stopped and account route state cleared",
			logevt::login::kAccountRouteDown);
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
			"[{}] sid={} serial={} server_id={} server_name={} listen_port={}",
			logevt::login::kAccountRouteReady,
 			sid, serial, server_id, server_name, listen_port);
	}

	void LoginLineRuntime::MarkAccountDisconnected(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		const auto cur_sid = account_sid_.load(std::memory_order_relaxed);
		const auto cur_serial = account_serial_.load(std::memory_order_relaxed);

		if (cur_sid != 0 && !dc::IsSameSessionKey(cur_sid, cur_serial, sid, serial)) {
			return;
		}

		account_ready_.store(false, std::memory_order_release);
		account_sid_.store(0, std::memory_order_relaxed);
		account_serial_.store(0, std::memory_order_relaxed);
		account_server_id_.store(0, std::memory_order_relaxed);

		spdlog::warn("[{}] sid={} serial={} account coordinator route disconnected",
			logevt::login::kAccountRouteDown, sid, serial);
	}

	bool LoginLineRuntime::SendAccountAuthRequest_(const PendingLoginRequest& pending)
	{
		if (!account_handler_) {
			return false;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);
		if (!dc::IsValidSessionKey(sid, serial)) {
			return false;
		}

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
		std::string_view world_token,
		std::string_view world_host,
		std::uint16_t world_port,
		std::string_view fail_reason)
	{
		PendingLoginRequest pending{};

		{
			std::lock_guard lk(pending_login_mtx_);
			const auto it = pending_login_requests_.find(request_id);
			if (it == pending_login_requests_.end()) {
				spdlog::warn("[{}] dropped: request_id={} not found", logevt::login::kAuthReqDropped, request_id);
				return;
			}

			pending = std::move(it->second);
			pending_login_requests_.erase(it);
		}

		pending.world_host.assign(world_host.data(), world_host.size());
		pending.world_port = world_port;

		CompleteLoginRequest_(std::move(pending), ok, account_id, char_id, login_session, world_token, fail_reason);
	}

	void LoginLineRuntime::CompleteLoginRequest_(
		PendingLoginRequest pending,
		bool ok,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token,
		std::string_view fail_reason)
	{
		if (!ok) {
			SendLoginResultFail_(
				pending.client_sid,
				pending.client_serial,
				fail_reason.empty() ? "account_auth_failed" : fail_reason.data());
			spdlog::warn("[{}] denied sid={} serial={} login_id={} reason={}",
				logevt::login::kAuthFail,
				pending.client_sid,
				pending.client_serial,
				pending.login_id,
				fail_reason.empty() ? "account_auth_failed" : fail_reason);
			return;
		}

		if (pending.world_host.empty() || pending.world_port == 0) {
			SendLoginResultFail_(pending.client_sid, pending.client_serial, "invalid_world_endpoint");
			spdlog::warn("[{}] denied sid={} serial={} login_id={} reason=invalid_world_endpoint",
				logevt::login::kAuthFail, pending.client_sid, pending.client_serial, pending.login_id);

			return;
		}

		if (!IsValidAuthIdentity_(account_id, char_id, login_session, world_token)) {
			SendLoginResultFail_(pending.client_sid, pending.client_serial, "invalid_auth_identity");
			spdlog::warn("[{}] denied sid={} serial={} login_id={} reason=invalid_auth_identity",
				logevt::login::kAuthFail, pending.client_sid, pending.client_serial, pending.login_id);
			return;
		}

		std::vector<DuplicateSessionRef> victims;
		{
			std::lock_guard lk(login_sessions_mtx_);

			RemoveLoginSession_NoLock_(pending.client_sid, pending.client_serial);
			victims = CollectDuplicateSessions_NoLock_(account_id, char_id, pending.client_sid, pending.client_serial);
			for (const auto& v : victims) {
				RemoveLoginSession_NoLock_(v.sid, v.serial);
			}

			EraseDetachedWorldEnterState_NoLock_(login_session, world_token);

			auto& st = login_sessions_[pending.client_sid];
			st.sid = pending.client_sid;
			st.serial = pending.client_serial;
			st.logged_in = true;
			st.account_id = account_id;
			st.char_id = char_id;
			st.login_session.assign(login_session.data(), login_session.size());
			st.world_token.assign(world_token.data(), world_token.size());
			st.issued_at = std::chrono::steady_clock::now();
			st.expires_at = st.issued_at + dc::k_login_sessions_pending_client_sid;

			account_session_index_[account_id] = SessionRef{ pending.client_sid, pending.client_serial };
			char_session_index_[char_id] = SessionRef{ pending.client_sid, pending.client_serial };
			login_session_index_[st.login_session] = SessionRef{ pending.client_sid, pending.client_serial };
			world_token_index_[st.world_token] = SessionRef{ pending.client_sid, pending.client_serial };
		}

		if (!victims.empty()) {
			CloseDuplicateLoginSessions_(victims);
		}

		SendLoginResultSuccess_(
			pending.client_sid,
			pending.client_serial,
			account_id,
			char_id,
			login_session,
			world_token,
			pending.world_host,
			pending.world_port);

		spdlog::info(
			"[{}] login accepted. waiting world enter notify account_id={} char_id={} token={}",
			logevt::login::kAuthSuccess,
 			account_id,
 			char_id,
 			world_token);
	}

	void LoginLineRuntime::OnWorldEnterSuccessNotify(
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		std::lock_guard lk(login_sessions_mtx_);

		SessionRef resolved_ref{};
		if (!world_token.empty()) {
			const auto iwt = world_token_index_.find(std::string(world_token));
			if (iwt != world_token_index_.end()) {
				resolved_ref = iwt->second;
			}
		}

		if (!resolved_ref.valid() && !login_session.empty()) {
			const auto ils = login_session_index_.find(std::string(login_session));
			if (ils != login_session_index_.end()) {
				resolved_ref = ils->second;
			}
		}

		if (resolved_ref.valid()) {
			auto it = login_sessions_.find(resolved_ref.sid);
			if (it != login_sessions_.end() && it->second.serial == resolved_ref.serial) {
				const auto& st = it->second;
				if (st.account_id == account_id &&
					st.char_id == char_id &&
					st.login_session == login_session &&
					st.world_token == world_token)
				{
					const auto sid = st.sid;
					const auto serial = st.serial;
					RemoveLoginSession_NoLock_(sid, serial);
					EraseDetachedWorldEnterState_NoLock_(login_session, world_token);

					spdlog::info(
						"[{}] removed login session after world enter notify. sid={} serial={} account_id={} char_id={} token={}",
						logevt::login::kWorldEnterNotify,
						sid,
						serial,
						account_id,
						char_id,
						world_token);
					return;
				}

				spdlog::warn(
					"[{}] session mismatch on world enter notify. sid={} serial={} account_id={} char_id={}",
					logevt::login::kWorldEnterNotify,
					st.sid,
					st.serial,
					account_id,
					char_id);
			}
		}

		if (!world_token.empty()) {
			auto iwt = detached_world_enter_by_token_.find(std::string(world_token));
			if (iwt != detached_world_enter_by_token_.end()) {
				const auto& st = iwt->second;
				if (st.account_id == account_id &&
					st.char_id == char_id &&
					st.login_session == login_session)
				{
					EraseDetachedWorldEnterState_NoLock_(login_session, world_token);
					spdlog::info(
						"[{}] consumed detached world enter notify. account_id={} char_id={} token={}",
						logevt::login::kWorldEnterNotify,
						account_id,
						char_id,
						world_token);
					return;
				}
 			}
 		}
 
		if (!login_session.empty()) {
			auto ils = detached_world_enter_token_by_login_session_.find(std::string(login_session));
			if (ils != detached_world_enter_token_by_login_session_.end()) {
				auto iwt = detached_world_enter_by_token_.find(ils->second);
				if (iwt != detached_world_enter_by_token_.end()) {
					const auto& st = iwt->second;
					if (st.account_id == account_id &&
						st.char_id == char_id &&
						st.world_token == world_token)
					{
						EraseDetachedWorldEnterState_NoLock_(login_session, world_token);
						spdlog::info(
							"[{}] consumed detached world enter notify by login_session. account_id={} char_id={} login_session={}",
							logevt::login::kWorldEnterNotify,
							account_id,
							char_id,
							login_session);
						return;
					}
				}
			}
 		}
 
		spdlog::warn(
			"[{}] exact session not found. account_id={} char_id={} login_session={} token={}",
			logevt::login::kWorldEnterNotify,
			account_id,
			char_id,
			login_session,
			world_token);
	}

	void LoginLineRuntime::ExpirePendingLoginRequests_(std::chrono::steady_clock::time_point now)
	{
		std::vector<PendingLoginRequest> expired;

		{
			std::lock_guard lk(pending_login_mtx_);
			for (auto it = pending_login_requests_.begin(); it != pending_login_requests_.end();) {
				if (now - it->second.issued_at >= dc::k_login_request_timeout) {
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

	void LoginLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now)
	{
		ExpirePendingLoginRequests_(now);
	}

} // namespace dc
