#include "services/login/runtime/login_line_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <filesystem>
#include <fstream>
#include <inipp/inipp.h>

#include <spdlog/spdlog.h>

#include "proto/client/login_proto.h"
#include "proto/common/packet_util.h"
#include "proto/common/protobuf_packet_codec.h"
#include "server_common/runtime/line_client_start_helper.h"
#include "server_common/runtime/line_start_helper.h"
#include "server_common/session/session_key.h"
#include "server_common/log/flow_event_codes.h"
#include "server_common/log/enter_flow_log.h"

#include "shared/constants.h"

#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_login.pb.h")
#include "proto/generated/cpp/client_login.pb.h"
#include "proto/generated/cpp/common.pb.h"
#define DC_LOGIN_FIRST_PATH_PROTOBUF 1
#else
#define DC_LOGIN_FIRST_PATH_PROTOBUF 0
#endif

namespace {
#if DC_LOGIN_FIRST_PATH_PROTOBUF
    dc::proto::common::WorldEntryNode MakeWorldEntryNodeFromSummary_(const ::proto::internal::login_account::WorldSummary& summary)
    {
        dc::proto::common::WorldEntryNode entry;
        entry.set_world_id(summary.world_id);
        entry.set_server_code(summary.server_name);
        entry.set_display_name(summary.server_name);
        entry.set_region("");
        entry.set_status(dc::proto::common::SERVICE_STATUS_UP);
        entry.set_recommended(false);
        entry.set_population(summary.active_zone_count);
        entry.set_capacity(0);
        entry.set_transfer_policy(dc::proto::common::TRANSFER_POLICY_DIRECT_WORLD);
        auto* endpoint = entry.add_endpoints();
        endpoint->set_host(summary.public_host);
        endpoint->set_port(summary.public_port);
        endpoint->set_region("");
        return entry;
    }

    dc::proto::common::WorldEntryNode MakeWorldEntryNodeFromEndpoint_(std::uint16_t world_id, std::string_view host, std::uint16_t port)
    {
        dc::proto::common::WorldEntryNode entry;
        entry.set_world_id(world_id);
        entry.set_server_code("world_" + std::to_string(world_id));
        entry.set_display_name("World " + std::to_string(world_id));
        entry.set_region("");
        entry.set_status(dc::proto::common::SERVICE_STATUS_UP);
        entry.set_recommended(false);
        entry.set_population(0);
        entry.set_capacity(0);
        entry.set_transfer_policy(dc::proto::common::TRANSFER_POLICY_DIRECT_WORLD);
        auto* endpoint = entry.add_endpoints();
        endpoint->set_host(std::string(host));
        endpoint->set_port(port);
        endpoint->set_region("");
        return entry;
    }
#endif
}

namespace pt_l = ::proto::login;

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
		bool use_protobuf)
	{
		if (!IsAccountReady()) {
			spdlog::warn("[{}] blocked: account route not ready sid={}", logevt::login::kAuthReqDropped, sid);
			return false;
		}

		PendingLoginRequest pending{};
		pending.trace_id = next_enter_trace_id_.fetch_add(1, std::memory_order_relaxed);
		pending.request_id = next_login_request_id_.fetch_add(1, std::memory_order_relaxed);
		pending.client_sid = sid;
		pending.client_serial = serial;
		pending.use_protobuf = use_protobuf;
		pending.login_id.assign(login_id.data(), login_id.size());
		pending.password.assign(password.data(), password.size());
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_login_requests_[pending.request_id] = pending;
		}

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::ClientLoginRequestReceived,
			{ pending.trace_id, 0, 0, sid, serial, {}, {} });

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
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::LoginAccountAuthRequestSent,
			{ pending.trace_id, 0, 0, sid, serial, {}, {} });

		return true;
	}

	bool LoginLineRuntime::IssueWorldListRequest(
		std::uint32_t sid,
		std::uint32_t serial,
		bool use_protobuf)
	{
		LoginSessionAuthState session{};
		{
			std::lock_guard lk(login_sessions_mtx_);
			const auto it = login_sessions_.find(sid);
			if (it == login_sessions_.end() || it->second.serial != serial || !it->second.logged_in) {
				return SendWorldListResult_(sid, serial, false, 0, nullptr, "not_logged_in", use_protobuf);
			}
			session = it->second;
		}

		PendingWorldListRequest pending{};
		pending.trace_id = session.trace_id != 0 ? session.trace_id : next_enter_trace_id_.fetch_add(1, std::memory_order_relaxed);
		pending.request_id = next_login_request_id_.fetch_add(1, std::memory_order_relaxed);
		pending.client_sid = sid;
		pending.client_serial = serial;
		pending.use_protobuf = use_protobuf;
		pending.account_id = session.account_id;
		pending.login_session = session.login_session;
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_world_list_requests_[pending.request_id] = pending;
		}

		if (!SendWorldListRequest_(pending)) {
			std::lock_guard lk(pending_login_mtx_);
			pending_world_list_requests_.erase(pending.request_id);
			return SendWorldListResult_(sid, serial, false, 0, nullptr, "account_route_not_ready", use_protobuf);
		}

		return true;
	}

	bool LoginLineRuntime::IssueWorldSelectRequest(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		bool use_protobuf)
	{
		LoginSessionAuthState session{};
		{
			std::lock_guard lk(login_sessions_mtx_);
			const auto it = login_sessions_.find(sid);
			if (it == login_sessions_.end() || it->second.serial != serial || !it->second.logged_in) {
				return SendWorldSelectResult_(sid, serial, false, world_id, {}, 0, pt_l::WorldSelectFailReason::not_logged_in, use_protobuf);
			}
			session = it->second;
		}

		PendingWorldSelectRequest pending{};
		pending.trace_id = session.trace_id != 0 ? session.trace_id : next_enter_trace_id_.fetch_add(1, std::memory_order_relaxed);
		pending.request_id = next_login_request_id_.fetch_add(1, std::memory_order_relaxed);
		pending.client_sid = sid;
		pending.client_serial = serial;
		pending.use_protobuf = use_protobuf;
		pending.account_id = session.account_id;
		pending.world_id = world_id;
		pending.channel_id = channel_id;
		pending.login_session = session.login_session;
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_world_select_requests_[pending.request_id] = pending;
		}

		if (!SendWorldSelectRequest_(pending)) {
			std::lock_guard lk(pending_login_mtx_);
			pending_world_select_requests_.erase(pending.request_id);
			return SendWorldSelectResult_(sid, serial, false, world_id, {}, 0, pt_l::WorldSelectFailReason::internal_error, use_protobuf);
		}

		return true;
	}

bool LoginLineRuntime::IssueCharacterListRequest(
	std::uint32_t sid,
	std::uint32_t serial,
	bool use_protobuf)
	{
		LoginSessionAuthState session{};
		{
			std::lock_guard lk(login_sessions_mtx_);
			const auto it = login_sessions_.find(sid);
			if (it == login_sessions_.end() || it->second.serial != serial || !it->second.logged_in) {
				return SendCharacterListResult_(sid, serial, false, 0, nullptr, "not_logged_in", use_protobuf);
			}
			session = it->second;
		}

		if (session.selected_world_id == 0) {
			return SendCharacterListResult_(sid, serial, false, 0, nullptr, "world_not_selected", use_protobuf);
		}

		PendingCharacterListRequest pending{};
		pending.trace_id = session.trace_id != 0 ? session.trace_id : next_enter_trace_id_.fetch_add(1, std::memory_order_relaxed);
		pending.request_id = next_login_request_id_.fetch_add(1, std::memory_order_relaxed);
		pending.client_sid = sid;
		pending.client_serial = serial;
		pending.use_protobuf = use_protobuf;
		pending.account_id = session.account_id;
		pending.world_id = session.selected_world_id;
		pending.login_session = session.login_session;
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_character_list_requests_[pending.request_id] = pending;
		}

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::CharacterListRequestReceived,
			{ pending.trace_id, pending.account_id, session.char_id, sid, serial, pending.login_session, session.world_token });

		if (!SendCharacterListRequest_(pending)) {
			std::lock_guard lk(pending_login_mtx_);
			pending_character_list_requests_.erase(pending.request_id);
			return SendCharacterListResult_(sid, serial, false, 0, nullptr, "account_route_not_ready", use_protobuf);
		}

		return true;
	}

bool LoginLineRuntime::IssueCharacterSelectRequest(
	std::uint32_t sid,
	std::uint32_t serial,
	std::uint64_t char_id,
	bool use_protobuf)
	{
		LoginSessionAuthState session{};
		{
			std::lock_guard lk(login_sessions_mtx_);
			const auto it = login_sessions_.find(sid);
			if (it == login_sessions_.end() || it->second.serial != serial || !it->second.logged_in) {
				return SendCharacterSelectResult_(
					sid,
					serial,
					false,
					0,
					0,
					{},
					{},
					0,
					pt_l::CharacterSelectFailReason::not_logged_in,
					use_protobuf);
			}
			session = it->second;
		}

		if (session.selected_world_id == 0) {
			return SendCharacterSelectResult_(
				sid,
				serial,
				false,
				session.account_id,
				char_id,
				{},
				{},
				0,
				pt_l::CharacterSelectFailReason::world_not_selected,
				use_protobuf);
		}

		PendingCharacterSelectRequest pending{};
		pending.trace_id = session.trace_id != 0 ? session.trace_id : next_enter_trace_id_.fetch_add(1, std::memory_order_relaxed);
		pending.request_id = next_login_request_id_.fetch_add(1, std::memory_order_relaxed);
		pending.client_sid = sid;
		pending.client_serial = serial;
		pending.use_protobuf = use_protobuf;
		pending.account_id = session.account_id;
		pending.char_id = char_id;
		pending.login_session = session.login_session;
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_login_mtx_);
			pending_character_select_requests_[pending.request_id] = pending;
		}

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::CharacterSelectRequestReceived,
			{ pending.trace_id, pending.account_id, pending.char_id, sid, serial, pending.login_session, session.world_token });

		if (!SendCharacterSelectRequest_(pending)) {
			std::lock_guard lk(pending_login_mtx_);
			pending_character_select_requests_.erase(pending.request_id);
			return SendCharacterSelectResult_(
				sid,
				serial,
				false,
				pending.account_id,
				pending.char_id,
				{},
				{},
				0,
				pt_l::CharacterSelectFailReason::internal_error,
				use_protobuf);
		}

		return true;
	}

	void LoginLineRuntime::RemoveLoginSession(std::uint32_t sid, std::uint32_t serial)
	{
		std::lock_guard lk(login_sessions_mtx_);
		RemoveLoginSession_NoLock_(sid, serial);
	}

	
	bool LoginLineRuntime::LoadIniFile_()
	{
		namespace fs = std::filesystem;
		const fs::path cwd = fs::current_path();
		fs::path ini_path = cwd / "docs" / "LoginSystem.ini";
		if (!fs::exists(ini_path)) ini_path = cwd / "Initialize" / "LoginSystem.ini";
		if (!fs::exists(ini_path)) ini_path = cwd / "LoginSystem.ini";
		std::ifstream is(ini_path, std::ios::in | std::ios::binary);
		if (!is) {
			spdlog::warn("LoginLineRuntime INI open failed: {}", ini_path.string());
			return false;
		}
		inipp::Ini<char> ini; ini.parse(is);
		if (!ini.sections["Config"]["Port"].empty()) port_ = static_cast<std::uint16_t>(std::stoi(ini.sections["Config"]["Port"]));
		if (!ini.sections["Account"]["AccountAddress"].empty()) account_host_ = ini.sections["Account"]["AccountAddress"];
		if (!ini.sections["Account"]["AccountPort"].empty()) account_port_ = static_cast<std::uint16_t>(std::stoi(ini.sections["Account"]["AccountPort"]));
		spdlog::info("LoginLineRuntime INI loaded. client_port={} account_remote={}:{}", port_, account_host_, account_port_);
		return true;
	}

bool LoginLineRuntime::OnRuntimeInit()
	{
		LoadIniFile_();
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

		account_handler_ = std::make_shared<LoginAccountHandler>(*this);


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
			pending_world_list_requests_.clear();
			pending_world_select_requests_.clear();
			pending_character_list_requests_.clear();
			pending_character_select_requests_.clear();
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

	void LoginLineRuntime::OnAccountRegistered(
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

	void LoginLineRuntime::OnAccountDisconnected(
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
			pending.trace_id,
			pending.request_id,
			pending.login_id,
			pending.password);
	}

	bool LoginLineRuntime::SendWorldListRequest_(const PendingWorldListRequest& pending)
	{
		if (!account_handler_) {
			return false;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);
		if (!dc::IsValidSessionKey(sid, serial)) {
			return false;
		}

		return account_handler_->SendWorldListRequest(
			2,
			sid,
			serial,
			pending.trace_id,
			pending.request_id,
			pending.account_id,
			pending.login_session);
	}

	bool LoginLineRuntime::SendWorldSelectRequest_(const PendingWorldSelectRequest& pending)
	{
		if (!account_handler_) {
			return false;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);
		if (!dc::IsValidSessionKey(sid, serial)) {
			return false;
		}

		return account_handler_->SendWorldSelectRequest(
			2,
			sid,
			serial,
			pending.trace_id,
			pending.request_id,
			pending.account_id,
			pending.world_id,
			pending.channel_id,
			pending.login_session);
	}

	bool LoginLineRuntime::SendCharacterListRequest_(const PendingCharacterListRequest& pending)
	{
		if (!account_handler_) {
			return false;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);
		if (!dc::IsValidSessionKey(sid, serial)) {
			return false;
		}

		return account_handler_->SendCharacterListRequest(
			2,
			sid,
			serial,
			pending.trace_id,
			pending.request_id,
			pending.account_id,
			pending.world_id,
			pending.login_session);
	}

	bool LoginLineRuntime::SendCharacterSelectRequest_(const PendingCharacterSelectRequest& pending)
	{
		if (!account_handler_) {
			return false;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);
		if (!dc::IsValidSessionKey(sid, serial)) {
			return false;
		}

		return account_handler_->SendCharacterSelectRequest(
			2,
			sid,
			serial,
			pending.trace_id,
			pending.request_id,
			pending.account_id,
			pending.char_id,
			pending.login_session);
	}

	void LoginLineRuntime::OnAccountAuthResult(
		std::uint64_t trace_id,
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
				dc::enterlog::LogEnterFlow(
					spdlog::level::warn,
					dc::enterlog::EnterStage::EnterFlowAborted,
					{ trace_id, account_id, char_id, 0, 0, login_session, world_token },
					"pending_request_missing");
				return;
			}

			pending = std::move(it->second);
			pending_login_requests_.erase(it);
		}
		dc::enterlog::LogEnterFlow(
			ok ? spdlog::level::info : spdlog::level::warn,
			dc::enterlog::EnterStage::AccountAuthResultReceived,
			{ trace_id, account_id, char_id, pending.client_sid, pending.client_serial, login_session, world_token },
			ok ? std::string_view{} : fail_reason);

		CompleteLoginRequest_(std::move(pending), ok, account_id, char_id, login_session, world_token, fail_reason);
	}

	void LoginLineRuntime::OnWorldListResult(
		std::uint64_t trace_id,
		std::uint64_t request_id,
		bool ok,
		std::uint64_t account_id,
		std::uint16_t count,
		const ::proto::internal::login_account::WorldSummary* worlds,
		std::string_view fail_reason)
	{
		PendingWorldListRequest pending{};
		{
			std::lock_guard lk(pending_login_mtx_);
			const auto it = pending_world_list_requests_.find(request_id);
			if (it == pending_world_list_requests_.end()) {
				return;
			}
			pending = std::move(it->second);
			pending_world_list_requests_.erase(it);
		}

		SendWorldListResult_(pending.client_sid, pending.client_serial, ok, count, worlds, fail_reason, pending.use_protobuf);
		dc::enterlog::LogEnterFlow(
			ok ? spdlog::level::info : spdlog::level::warn,
			dc::enterlog::EnterStage::CharacterListResponseSent,
			{ trace_id, account_id, 0, pending.client_sid, pending.client_serial, pending.login_session, {} },
			ok ? std::string_view{} : fail_reason,
			"world_list");
	}

	void LoginLineRuntime::OnWorldSelectResult(
		std::uint64_t trace_id,
		std::uint64_t request_id,
		bool ok,
		std::uint64_t account_id,
		std::uint16_t world_id,
		std::string_view login_session,
		std::string_view world_host,
		std::uint16_t world_port,
		std::string_view fail_reason)
	{
		PendingWorldSelectRequest pending{};
		{
			std::lock_guard lk(pending_login_mtx_);
			const auto it = pending_world_select_requests_.find(request_id);
			if (it == pending_world_select_requests_.end()) {
				return;
			}
			pending = std::move(it->second);
			pending_world_select_requests_.erase(it);
		}

		if (ok) {
			std::lock_guard lk(login_sessions_mtx_);
			auto it = login_sessions_.find(pending.client_sid);
			if (it != login_sessions_.end() && it->second.serial == pending.client_serial && it->second.account_id == account_id) {
				auto& st = it->second;
				st.selected_world_id = world_id;
				st.world_host.assign(world_host.data(), world_host.size());
				st.world_port = world_port;
				st.world_token.clear();
			}
		}

		const auto fail_code =
			ok ? pt_l::WorldSelectFailReason::success :
			(fail_reason == "world_not_found" ? pt_l::WorldSelectFailReason::world_not_found :
			 fail_reason == "world_not_selectable" ? pt_l::WorldSelectFailReason::world_not_selectable :
			 fail_reason == "invalid_login_session" ? pt_l::WorldSelectFailReason::invalid_login_session :
			 pt_l::WorldSelectFailReason::internal_error);

		SendWorldSelectResult_(pending.client_sid, pending.client_serial, ok, world_id, world_host, world_port, fail_code, pending.use_protobuf);
	}

	void LoginLineRuntime::OnCharacterListResult(
		std::uint64_t trace_id,
		std::uint64_t request_id,
		bool ok,
		std::uint64_t account_id,
		std::uint16_t count,
		const ::proto::internal::login_account::CharacterSummary* characters,
		std::string_view fail_reason)
	{
		PendingCharacterListRequest pending{};
		{
			std::lock_guard lk(pending_login_mtx_);
			const auto it = pending_character_list_requests_.find(request_id);
			if (it == pending_character_list_requests_.end()) {
				return;
			}
			pending = std::move(it->second);
			pending_character_list_requests_.erase(it);
		}

		SendCharacterListResult_(pending.client_sid, pending.client_serial, ok, count, characters, fail_reason, pending.use_protobuf);
		dc::enterlog::LogEnterFlow(
			ok ? spdlog::level::info : spdlog::level::warn,
			dc::enterlog::EnterStage::CharacterListResponseSent,
			{ trace_id, account_id, 0, pending.client_sid, pending.client_serial, pending.login_session, {} },
			ok ? std::string_view{} : fail_reason);
	}

	void LoginLineRuntime::OnCharacterSelectResult(
		std::uint64_t trace_id,
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
		PendingCharacterSelectRequest pending{};
		{
			std::lock_guard lk(pending_login_mtx_);
			const auto it = pending_character_select_requests_.find(request_id);
			if (it == pending_character_select_requests_.end()) {
				return;
			}
			pending = std::move(it->second);
			pending_character_select_requests_.erase(it);
		}

		if (ok) {
			std::lock_guard lk(login_sessions_mtx_);
			auto it = login_sessions_.find(pending.client_sid);
			if (it != login_sessions_.end() && it->second.serial == pending.client_serial) {
				auto& st = it->second;
				if (st.char_id != 0) {
					auto ic = char_session_index_.find(st.char_id);
					if (ic != char_session_index_.end() &&
						dc::IsSameSessionKey(ic->second.sid, ic->second.serial, pending.client_sid, pending.client_serial)) {
						char_session_index_.erase(ic);
					}
				}
				if (!st.world_token.empty()) {
					auto iwt = world_token_index_.find(st.world_token);
					if (iwt != world_token_index_.end() &&
						dc::IsSameSessionKey(iwt->second.sid, iwt->second.serial, pending.client_sid, pending.client_serial)) {
						world_token_index_.erase(iwt);
					}
				}
				st.char_id = char_id;
				st.world_token.assign(world_token.data(), world_token.size());
				st.world_host.assign(world_host.data(), world_host.size());
				st.world_port = world_port;
				char_session_index_[char_id] = SessionRef{ pending.client_sid, pending.client_serial };
				world_token_index_[st.world_token] = SessionRef{ pending.client_sid, pending.client_serial };
			}
		}

		const auto fail_code =
			ok ? pt_l::CharacterSelectFailReason::success :
			(fail_reason == "character_not_found" ? pt_l::CharacterSelectFailReason::character_not_found :
			 fail_reason == "character_account_mismatch" ? pt_l::CharacterSelectFailReason::character_account_mismatch :
			 fail_reason == "invalid_login_session" ? pt_l::CharacterSelectFailReason::invalid_login_session :
			 fail_reason == "world_not_ready" ? pt_l::CharacterSelectFailReason::world_not_ready :
			 fail_reason == "world_not_selected" ? pt_l::CharacterSelectFailReason::world_not_selected :
			 fail_reason == "character_not_selectable" ? pt_l::CharacterSelectFailReason::character_not_selectable :
			 pt_l::CharacterSelectFailReason::internal_error);

		SendCharacterSelectResult_(
			pending.client_sid,
			pending.client_serial,
			ok,
			account_id,
			char_id,
			world_token,
			world_host,
			world_port,
			fail_code,
			pending.use_protobuf);
		dc::enterlog::LogEnterFlow(
			ok ? spdlog::level::info : spdlog::level::warn,
			ok ? dc::enterlog::EnterStage::CharacterSelectSuccess : dc::enterlog::EnterStage::CharacterSelectFailed,
			{ trace_id, account_id, char_id, pending.client_sid, pending.client_serial, login_session, world_token },
			ok ? std::string_view{} : fail_reason);
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
				fail_reason.empty() ? "account_auth_failed" : fail_reason.data(),
				pending.use_protobuf);
			spdlog::warn("[{}] denied sid={} serial={} login_id={} reason={}",
				logevt::login::kAuthFail,
				pending.client_sid,
				pending.client_serial,
				pending.login_id,
				fail_reason.empty() ? "account_auth_failed" : fail_reason);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, 0, 0, pending.client_sid, pending.client_serial, {}, {} },
				fail_reason.empty() ? std::string_view("account_auth_failed") : fail_reason);
			return;
		}

		if (account_id == 0 || login_session.empty()) {
			SendLoginResultFail_(pending.client_sid, pending.client_serial, "invalid_auth_identity", pending.use_protobuf);
			spdlog::warn("[{}] denied sid={} serial={} login_id={} reason=invalid_auth_identity",
				logevt::login::kAuthFail, pending.client_sid, pending.client_serial, pending.login_id);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, account_id, 0, pending.client_sid, pending.client_serial, login_session, world_token },
				"invalid_auth_identity");
			return;
		}

		std::vector<DuplicateSessionRef> victims;
		{
			std::lock_guard lk(login_sessions_mtx_);

			RemoveLoginSession_NoLock_(pending.client_sid, pending.client_serial);
			victims = CollectDuplicateSessions_NoLock_(account_id, 0, pending.client_sid, pending.client_serial);
			for (const auto& v : victims) {
				RemoveLoginSession_NoLock_(v.sid, v.serial);
			}

			EraseDetachedWorldEnterState_NoLock_(login_session, world_token);

			auto& st = login_sessions_[pending.client_sid];
			st.sid = pending.client_sid;
			st.serial = pending.client_serial;
			st.trace_id = pending.trace_id;
			st.logged_in = true;
			st.account_id = account_id;
			st.char_id = 0;
			st.selected_world_id = 0;
			st.world_host.clear();
			st.world_port = 0;
			st.login_session.assign(login_session.data(), login_session.size());
			st.world_token.clear();
			st.issued_at = std::chrono::steady_clock::now();
			st.expires_at = st.issued_at + dc::k_login_sessions_pending_client_sid;

			account_session_index_[account_id] = SessionRef{ pending.client_sid, pending.client_serial };
			login_session_index_[st.login_session] = SessionRef{ pending.client_sid, pending.client_serial };
		}

		if (!victims.empty()) {
			CloseDuplicateLoginSessions_(victims);
		}

		SendLoginResultSuccess_(
			pending.client_sid,
			pending.client_serial,
			account_id,
			0,
			login_session,
			{},
			{},
			0,
			pending.use_protobuf);

		spdlog::info(
			"[{}] login accepted. waiting character selection account_id={}",
			logevt::login::kAuthSuccess,
 			account_id);
	}

	void LoginLineRuntime::OnWorldEnterSuccessNotify(
		std::uint64_t trace_id,
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
					dc::enterlog::LogEnterFlow(
						spdlog::level::info,
						dc::enterlog::EnterStage::LoginWorldEnterNotifyReceived,
						{ trace_id, account_id, char_id, sid, serial, login_session, world_token });
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
					dc::enterlog::LogEnterFlow(
						spdlog::level::info,
						dc::enterlog::EnterStage::LoginWorldEnterNotifyReceived,
						{ trace_id, account_id, char_id, 0, 0, login_session, world_token },
						{},
						"detached_by_token");
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
						dc::enterlog::LogEnterFlow(
							spdlog::level::info,
							dc::enterlog::EnterStage::LoginWorldEnterNotifyReceived,
							{ trace_id, account_id, char_id, 0, 0, login_session, world_token },
							{},
							"detached_by_login_session");
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
		dc::enterlog::LogEnterFlow(
			spdlog::level::warn,
			dc::enterlog::EnterStage::EnterFlowAborted,
			{ trace_id, account_id, char_id, 0, 0, login_session, world_token },
			"notify_target_not_found");
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
			SendLoginResultFail_(e.client_sid, e.client_serial, "account_timeout", e.use_protobuf);
		}
	}

	bool LoginLineRuntime::SendLoginResultFail_(
		std::uint32_t sid,
		std::uint32_t serial,
		const char* reason,
		bool use_protobuf)
	{
		if (!login_handler_) {
			return false;
		}

#if DC_LOGIN_FIRST_PATH_PROTOBUF
		if (use_protobuf) {
			dc::proto::client::login::LoginResult res;
			res.set_ok(false);
			if (reason != nullptr) {
				res.set_fail_reason(reason);
			}
			std::vector<char> framed;
			if (!dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result), res, framed)) {
				return false;
			}
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			return login_handler_->Send(0, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
		}
#endif

		pt_l::S2C_login_result res{};
		res.ok = 0;
		const auto h = ::proto::make_header(
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
		std::uint16_t world_port,
		bool use_protobuf)
	{
		if (!login_handler_) {
			return false;
		}

#if DC_LOGIN_FIRST_PATH_PROTOBUF
		if (use_protobuf) {
			dc::proto::client::login::LoginResult res;
			res.set_ok(true);
			res.set_account_id(account_id);
			res.set_login_session(std::string(login_session));
			std::vector<char> framed;
			if (!dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result), res, framed)) {
				return false;
			}
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			return login_handler_->Send(0, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
		}
#endif

		pt_l::S2C_login_result res{};
		res.ok = 1;
		res.account_id = account_id;
		res.char_id = char_id;
		res.world_port = world_port;

		std::snprintf(res.login_session, sizeof(res.login_session), "%.*s", static_cast<int>(login_session.size()), login_session.data());
		std::snprintf(res.world_host, sizeof(res.world_host), "%.*s", static_cast<int>(world_host.size()), world_host.data());
		std::snprintf(res.world_token, sizeof(res.world_token), "%.*s", static_cast<int>(token.size()), token.data());

		const auto h = ::proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result),
			static_cast<std::uint16_t>(sizeof(res)));
		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

	bool LoginLineRuntime::SendWorldListResult_(
		std::uint32_t sid,
		std::uint32_t serial,
		bool ok,
		std::uint16_t count,
		const ::proto::internal::login_account::WorldSummary* worlds,
		std::string_view fail_reason,
		bool use_protobuf)
	{
		if (!login_handler_) {
			return false;
		}

#if DC_LOGIN_FIRST_PATH_PROTOBUF
		if (use_protobuf) {
			dc::proto::client::login::WorldListResponse res;
			res.set_ok(ok);
			res.set_fail_reason(std::string(fail_reason));
			if (worlds != nullptr) {
				for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_world_list_max_count); ++i) {
					*res.add_world_entries() = MakeWorldEntryNodeFromSummary_(worlds[i]);
				}
			}
			std::vector<char> framed;
			if (!dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_l::LoginS2CMsg::world_list_response), res, framed)) {
				return false;
			}
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			return login_handler_->Send(0, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
		}
#endif

		pt_l::S2C_world_list_response res{};
		res.ok = ok ? 1 : 0;
		res.count = count;
		std::snprintf(res.fail_reason, sizeof(res.fail_reason), "%.*s", static_cast<int>(fail_reason.size()), fail_reason.data());
		if (worlds != nullptr) {
			for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_world_list_max_count); ++i) {
				res.worlds[i].world_id = worlds[i].world_id;
				res.worlds[i].channel_id = worlds[i].channel_id;
				res.worlds[i].active_zone_count = worlds[i].active_zone_count;
				res.worlds[i].load_score = worlds[i].load_score;
				res.worlds[i].public_port = worlds[i].public_port;
				res.worlds[i].flags = worlds[i].flags;
				std::snprintf(res.worlds[i].server_name, sizeof(res.worlds[i].server_name), "%s", worlds[i].server_name);
				std::snprintf(res.worlds[i].public_host, sizeof(res.worlds[i].public_host), "%s", worlds[i].public_host);
			}
		}

		const auto h = ::proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::world_list_response),
			static_cast<std::uint16_t>(sizeof(res)));
		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

	bool LoginLineRuntime::SendWorldSelectResult_(
		std::uint32_t sid,
		std::uint32_t serial,
		bool ok,
		std::uint16_t world_id,
		std::string_view world_host,
		std::uint16_t world_port,
		pt_l::WorldSelectFailReason fail_reason,
		bool use_protobuf)
	{
		if (!login_handler_) {
			return false;
		}

#if DC_LOGIN_FIRST_PATH_PROTOBUF
		if (use_protobuf) {
			dc::proto::client::login::WorldSelectResponse res;
			res.set_ok(ok);
			res.set_world_id(world_id);
			res.set_server_code("world_" + std::to_string(world_id));
			res.set_fail_reason(ok ? std::string() : std::to_string(static_cast<std::uint16_t>(fail_reason)));
			*res.mutable_selected_entry() = MakeWorldEntryNodeFromEndpoint_(world_id, world_host, world_port);
			std::vector<char> framed;
			if (!dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_l::LoginS2CMsg::world_select_response), res, framed)) {
				return false;
			}
			_MSG_HEADER header{};
			std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
			return login_handler_->Send(0, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
		}
#endif

		pt_l::S2C_world_select_response res{};
		res.ok = ok ? 1 : 0;
		res.fail_reason = static_cast<std::uint16_t>(fail_reason);
		res.world_id = world_id;
		res.world_port = world_port;
		std::snprintf(res.world_host, sizeof(res.world_host), "%.*s", static_cast<int>(world_host.size()), world_host.data());

		const auto h = ::proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::world_select_response),
			static_cast<std::uint16_t>(sizeof(res)));
		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

bool LoginLineRuntime::SendCharacterListResult_(
	std::uint32_t sid,
	std::uint32_t serial,
	bool ok,
	std::uint16_t count,
	const ::proto::internal::login_account::CharacterSummary* characters,
	std::string_view fail_reason,
	bool use_protobuf)
{
	if (!login_handler_) {
		return false;
	}

#if DC_LOGIN_FIRST_PATH_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::login::CharacterListResponse res;
		res.set_ok(ok);
		res.set_fail_reason(std::string(fail_reason));
		if (characters != nullptr) {
			for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_character_list_max_count); ++i) {
				auto* out = res.add_characters();
				out->set_char_id(characters[i].char_id);
				out->set_name(characters[i].char_name);
				out->set_level(characters[i].level);
				out->set_job(characters[i].job);
				out->set_appearance_code(characters[i].appearance_code);
				out->set_last_login_at_epoch_sec(characters[i].last_login_at_epoch_sec);
			}
		}
		std::vector<char> framed;
		if (!dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_l::LoginS2CMsg::character_list_response), res, framed)) {
			return false;
		}
		_MSG_HEADER header{};
		std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
		return login_handler_->Send(0, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
	}
#endif

	pt_l::S2C_character_list_response res{};
		res.ok = ok ? 1 : 0;
		res.count = count;
		std::snprintf(res.fail_reason, sizeof(res.fail_reason), "%.*s",
			static_cast<int>(fail_reason.size()), fail_reason.data());
		if (characters != nullptr) {
			for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_character_list_max_count); ++i) {
				res.characters[i].char_id = characters[i].char_id;
				std::snprintf(res.characters[i].char_name, sizeof(res.characters[i].char_name), "%s", characters[i].char_name);
				res.characters[i].level = characters[i].level;
				res.characters[i].job = characters[i].job;
				res.characters[i].appearance_code = characters[i].appearance_code;
				res.characters[i].last_login_at_epoch_sec = characters[i].last_login_at_epoch_sec;
			}
		}

		const auto h = ::proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::character_list_response),
			static_cast<std::uint16_t>(sizeof(res)));
		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

bool LoginLineRuntime::SendCharacterSelectResult_(
	std::uint32_t sid,
	std::uint32_t serial,
	bool ok,
		std::uint64_t account_id,
	std::uint64_t char_id,
	std::string_view world_token,
	std::string_view world_host,
	std::uint16_t world_port,
	pt_l::CharacterSelectFailReason fail_reason,
	bool use_protobuf)
{
	if (!login_handler_) {
		return false;
	}

#if DC_LOGIN_FIRST_PATH_PROTOBUF
	if (use_protobuf) {
		dc::proto::client::login::CharacterSelectResponse res;
		res.set_ok(ok);
		res.set_account_id(account_id);
		res.set_char_id(char_id);
		res.set_world_token(std::string(world_token));
		if (ok || !world_host.empty() || world_port != 0) {
			*res.mutable_selected_entry() = MakeWorldEntryNodeFromEndpoint_(0, world_host, world_port);
		}
		res.set_fail_reason(ok ? std::string() : std::to_string(static_cast<std::uint16_t>(fail_reason)));
		std::vector<char> framed;
		if (!dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(pt_l::LoginS2CMsg::character_select_response), res, framed)) {
			return false;
		}
		_MSG_HEADER header{};
		std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
		return login_handler_->Send(0, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
	}
#endif

	pt_l::S2C_character_select_response res{};
		res.ok = ok ? 1 : 0;
		res.fail_reason = static_cast<std::uint16_t>(fail_reason);
		res.account_id = account_id;
		res.char_id = char_id;
		res.world_port = world_port;
		std::snprintf(res.world_token, sizeof(res.world_token), "%.*s",
			static_cast<int>(world_token.size()), world_token.data());
		std::snprintf(res.world_host, sizeof(res.world_host), "%.*s",
			static_cast<int>(world_host.size()), world_host.data());

		const auto h = ::proto::make_header(
			static_cast<std::uint16_t>(pt_l::LoginS2CMsg::character_select_response),
			static_cast<std::uint16_t>(sizeof(res)));
		return login_handler_->Send(0, sid, serial, h, reinterpret_cast<const char*>(&res));
	}

	void LoginLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now)
	{
		ExpirePendingLoginRequests_(now);
	}

} // namespace dc

























