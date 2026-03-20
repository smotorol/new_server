#include "services/account/runtime/account_line_runtime.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <ctime>
#include <iostream>

#include <spdlog/spdlog.h>

#include "services/account/db/account_auth_job.h"
#include "server_common/runtime/line_start_helper.h"
#include "services/account/db/account_auth_db_repository.h"
#include "services/account/security/login_session_token.h"
#include "services/account/handler/account_world_handler.h"
#include "services/world/runtime/i_world_runtime.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

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

namespace dc {

	AccountLineRuntime::AccountLineRuntime(
		std::uint16_t login_port,
		std::uint16_t world_port)
		: login_port_(login_port)
		, world_port_(world_port)
	{
	}

	void AccountLineRuntime::MarkLoginRegistered(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::string_view server_name,
		std::uint16_t listen_port)
	{
		login_sid_.store(sid, std::memory_order_relaxed);
		login_serial_.store(serial, std::memory_order_relaxed);
		login_ready_.store(true, std::memory_order_release);

		spdlog::info(
			"AccountLineRuntime login ready sid={} serial={} server_id={} server_name={} listen_port={}",
			sid, serial, server_id, server_name, listen_port);

		if (login_handler_) {
			login_handler_->SendRegisterAck(0, sid, serial, 1, 10, "account", login_port_);
		}
	}

	void AccountLineRuntime::MarkLoginDisconnected(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		const auto cur_sid = login_sid_.load(std::memory_order_relaxed);
		const auto cur_serial = login_serial_.load(std::memory_order_relaxed);

		if (cur_sid != sid || cur_serial != serial) {
			return;
		}

		login_ready_.store(false, std::memory_order_release);
		login_sid_.store(0, std::memory_order_relaxed);
		login_serial_.store(0, std::memory_order_relaxed);
	}

	void AccountLineRuntime::MarkWorldRegistered(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t active_zone_count,
		std::uint16_t load_score,
		std::uint32_t flags,
		std::string_view server_name,
		std::string_view public_host,
		std::uint16_t public_port)
	{
		RegisteredWorldEndpoint endpoint{};
		endpoint.sid = sid;
		endpoint.serial = serial;
		endpoint.server_id = server_id;
		endpoint.world_id = world_id;
		endpoint.channel_id = channel_id;
		endpoint.active_zone_count = active_zone_count;
		endpoint.load_score = load_score;
		endpoint.flags = flags;
		endpoint.server_name.assign(server_name.data(), server_name.size());
		endpoint.public_host.assign(public_host.data(), public_host.size());
		endpoint.public_port = public_port;
		endpoint.registered_at = std::chrono::steady_clock::now();
		endpoint.last_heartbeat_at = endpoint.registered_at;

		std::size_t world_count = 0;
		{
			std::lock_guard lk(world_registry_mtx_);

			if (server_id != 0) {
				const auto same_server = world_sid_by_server_id_.find(server_id);
				if (same_server != world_sid_by_server_id_.end() && same_server->second != sid) {
					worlds_by_sid_.erase(same_server->second);
				}
				world_sid_by_server_id_[server_id] = sid;
			}

			worlds_by_sid_[sid] = std::move(endpoint);
			world_count = worlds_by_sid_.size();
		}

		spdlog::info(
			"AccountLineRuntime world coordinator registered sid={} serial={} server_id={} world_id={} channel_id={} zones={} load_score={} flags={} server_name={} public_host={} public_port={} world_count={}",
			sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags, server_name, public_host, public_port, world_count);

		if (world_handler_) {
			world_handler_->SendRegisterAck(
				0,
				sid,
				serial,
				1,
				20,
				world_id,
				channel_id,
				active_zone_count,
				load_score,
				flags,
				"account",
				public_host,
				public_port);
		}
	}

	void AccountLineRuntime::MarkWorldDisconnected(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		std::uint32_t removed_server_id = 0;
		std::size_t world_count = 0;

		{
			std::lock_guard lk(world_registry_mtx_);
			const auto it = worlds_by_sid_.find(sid);
			if (it == worlds_by_sid_.end() || it->second.serial != serial) {
				return;
			}

			removed_server_id = it->second.server_id;
			worlds_by_sid_.erase(it);

			if (removed_server_id != 0) {
				const auto idx = world_sid_by_server_id_.find(removed_server_id);
				if (idx != world_sid_by_server_id_.end() && idx->second == sid) {
					world_sid_by_server_id_.erase(idx);
				}
			}

			world_count = worlds_by_sid_.size();
		}

		if (removed_server_id != 0) {
			ErasePendingWorldTicketsForServer_(removed_server_id);
		}

		spdlog::info(
			"AccountLineRuntime world disconnected sid={} serial={} server_id={} remaining_worlds={}",
			sid,
			serial,
			removed_server_id,
			world_count);
	}

	void AccountLineRuntime::OnWorldRouteHeartbeat(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t active_zone_count,
		std::uint16_t load_score,
		std::uint32_t flags)
	{
		std::lock_guard lk(world_registry_mtx_);
		const auto it = worlds_by_sid_.find(sid);
		if (it == worlds_by_sid_.end() || it->second.serial != serial) {
			spdlog::debug(
				"AccountLineRuntime ignored stale world route heartbeat. sid={} serial={} server_id={} world_id={} channel_id={}",
				sid,
				serial,
				server_id,
				world_id,
				channel_id);
			return;
		}

		auto& endpoint = it->second;
		if (endpoint.server_id != server_id) {
			spdlog::warn(
				"AccountLineRuntime ignored world heartbeat with mismatched server_id. sid={} serial={} endpoint_server_id={} heartbeat_server_id={}",
				sid,
				serial,
				endpoint.server_id,
				server_id);
			return;
		}

		endpoint.world_id = world_id;
		endpoint.channel_id = channel_id;
		endpoint.active_zone_count = active_zone_count;
		endpoint.load_score = load_score;
		endpoint.flags = flags;
		endpoint.last_heartbeat_at = std::chrono::steady_clock::now();
	}

	bool AccountLineRuntime::IsWorldSelectable_(const RegisteredWorldEndpoint& endpoint)
	{
		if (endpoint.public_host.empty() || endpoint.public_port == 0) {
			return false;
		}

		if ((endpoint.flags & pt_aw::k_world_flag_visible) == 0) {
			return false;
		}

		if ((endpoint.flags & pt_aw::k_world_flag_accepting_players) == 0) {
			return false;
		}

		return true;
	}

	bool AccountLineRuntime::TryGetWorldEndpoint_(
		std::string& out_host,
		std::uint16_t& out_port,
		std::uint32_t& out_server_id,
		std::uint16_t& out_world_id,
		std::uint16_t& out_channel_id) const
	{
		std::lock_guard lk(world_registry_mtx_);

		const RegisteredWorldEndpoint* selected = nullptr;
		for (const auto& [_, endpoint] : worlds_by_sid_) {
			if (!IsWorldSelectable_(endpoint)) {
				continue;
			}

			if (!selected ||
				endpoint.world_id < selected->world_id ||
				(endpoint.world_id == selected->world_id && endpoint.channel_id < selected->channel_id) ||
				(endpoint.world_id == selected->world_id && endpoint.channel_id == selected->channel_id && endpoint.load_score < selected->load_score) ||
				(endpoint.world_id == selected->world_id && endpoint.channel_id == selected->channel_id && endpoint.load_score == selected->load_score && endpoint.active_zone_count > selected->active_zone_count) ||
				(endpoint.world_id == selected->world_id && endpoint.channel_id == selected->channel_id && endpoint.load_score == selected->load_score && endpoint.active_zone_count == selected->active_zone_count && endpoint.server_id < selected->server_id)) {

				selected = &endpoint;
			}
		}

		if (!selected) {
			return false;
		}

		out_host = selected->public_host;
		out_port = selected->public_port;
		out_server_id = selected->server_id;
		out_world_id = selected->world_id;
		out_channel_id = selected->channel_id;
		return true;
	}

	bool AccountLineRuntime::TryGetRegisteredWorldServerId_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t& out_server_id) const
	{
		std::lock_guard lk(world_registry_mtx_);
		const auto it = worlds_by_sid_.find(sid);
		if (it == worlds_by_sid_.end() || it->second.serial != serial) {
			return false;
		}

		out_server_id = it->second.server_id;
		return out_server_id != 0;
	}

	void AccountLineRuntime::ErasePendingWorldTicketsForServer_(std::uint32_t world_server_id)
	{
		std::lock_guard lk(pending_world_upsert_mtx_);

		for (auto it = pending_world_upserts_.begin(); it != pending_world_upserts_.end();) {
			if (it->second.target_world_server_id == world_server_id) {
				it = pending_world_upserts_.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = consumed_world_enters_awaiting_notify_.begin(); it != consumed_world_enters_awaiting_notify_.end();) {
			if (it->second.target_world_server_id == world_server_id) {
				it = consumed_world_enters_awaiting_notify_.erase(it);
			}
			else {
				++it;
			}
		}
	}

	std::string AccountLineRuntime::GenerateWorldToken_() const
	{
		static thread_local std::mt19937_64 rng{ std::random_device{}() };
		return ToHexToken_(rng(), rng());
	}

	bool AccountLineRuntime::InitDqs_()
	{
		std::lock_guard lk(dqs_mtx_);

		dqs_slots_.assign(kMaxDqsSlotCount, {});
		dqs_empty_.clear();
		for (std::uint32_t i = 0; i < kMaxDqsSlotCount; ++i) {
			dqs_empty_.push_back(i);
		}

		dqs_push_count_.store(0, std::memory_order_relaxed);
		dqs_drop_count_.store(0, std::memory_order_relaxed);
		return true;
	}

	std::uint32_t AccountLineRuntime::RouteAccountShard_(
		const svr::dqs_payload::AccountAuthRequest& payload) const
	{
		if (db_shard_count_ <= 1) {
			return 0;
		}

		const std::string_view login_id(payload.login_id);
		std::uint64_t h = 1469598103934665603ull;
		for (char c : login_id) {
			h ^= static_cast<unsigned char>(c);
			h *= 1099511628211ull;
		}

		return static_cast<std::uint32_t>(h % db_shard_count_);
	}

	bool AccountLineRuntime::PushAccountAuthDqs_(
		const svr::dqs_payload::AccountAuthRequest& payload)
	{
		static_assert(sizeof(svr::dqs_payload::AccountAuthRequest) <= svr::dqs::DqsSlot::max_data_size);

		std::uint32_t idx = 0;
		{
			std::lock_guard lk(dqs_mtx_);

			if (dqs_empty_.empty()) {
				dqs_drop_count_.fetch_add(1, std::memory_order_relaxed);
				spdlog::warn("AccountLineRuntime DQS drop: no empty slot.");
				return false;
			}

			idx = dqs_empty_.front();
			dqs_empty_.pop_front();

			auto& slot = dqs_slots_[idx];
			slot.reset();
			slot.process_code = static_cast<std::uint8_t>(svr::dqs::ProcessCode::account);
			slot.qry_case = static_cast<std::uint8_t>(svr::dqs::QueryCase::account_auth);
			slot.result = svr::dqs::ResultCode::success;
			slot.in_use = true;
			slot.done = false;
			slot.data_size = static_cast<std::uint16_t>(sizeof(payload));
			std::memcpy(slot.data.data(), &payload, sizeof(payload));
		}

		if (!db_shards_) {
			RecycleDqsSlot_(idx);
			dqs_drop_count_.fetch_add(1, std::memory_order_relaxed);
			spdlog::error("AccountLineRuntime DQS drop: db_shards_ is null.");
			return false;
		}

		const auto shard = RouteAccountShard_(payload);
		dqs_push_count_.fetch_add(1, std::memory_order_relaxed);
		db_shards_->push(shard, idx);
		return true;
	}

	bool AccountLineRuntime::InitDbWorkers_()
	{
		db_worker_conns_.clear();
		db_worker_conns_.reserve(db_shard_count_);

		try {
			for (std::uint32_t i = 0; i < db_shard_count_; ++i) {
				auto conn = std::make_unique<db::OdbcConnection>();
				conn->connect(db_conn_str_);
				db_worker_conns_.push_back(std::move(conn));
			}
		}
		catch (const std::exception& e) {
			spdlog::error("AccountLineRuntime DB worker init exception: {}", e.what());
			db_worker_conns_.clear();
			return false;
		}

		db_shards_ = std::make_unique<svr::dbshard::DbShardManager>(
			db_shard_count_,
			[this](std::uint32_t slot_index) {
			OnDqsRunOne_(slot_index);
		},
			[this](std::uint32_t slot_index) {
			RecycleDqsSlot_(slot_index);
		});

		db_shards_->start();

		spdlog::info(
			"AccountLineRuntime DB workers started. shard_count={}",
			db_shard_count_);

		return true;
	}

	void AccountLineRuntime::ShutdownDbWorkers_()
	{
		if (db_shards_) {
			db_shards_->stop();
			db_shards_.reset();
		}

		db_worker_conns_.clear();
	}

	void AccountLineRuntime::OnDqsRunOne_(std::uint32_t slot_index)
	{
		if (slot_index >= dqs_slots_.size()) {
			return;
		}

		auto& slot = dqs_slots_[slot_index];
		const auto pc = static_cast<svr::dqs::ProcessCode>(slot.process_code);
		const auto qc = static_cast<svr::dqs::QueryCase>(slot.qry_case);

		if (pc != svr::dqs::ProcessCode::account ||
			qc != svr::dqs::QueryCase::account_auth)
		{
			slot.result = svr::dqs::ResultCode::invalid_data;
			return;
		}

		if (slot.data_size < sizeof(svr::dqs_payload::AccountAuthRequest)) {
			slot.result = svr::dqs::ResultCode::invalid_data;
			return;
		}

		svr::dqs_payload::AccountAuthRequest payload{};
		std::memcpy(&payload, slot.data.data(), sizeof(payload));

		auto rr = dc::account::MakePendingAccountAuthResult(payload);

		try {
			const auto shard = RouteAccountShard_(payload);
			if (shard >= db_worker_conns_.size() || !db_worker_conns_[shard]) {
				dc::account::SetAccountAuthDbError(rr, "db_conn_not_ready");
				PostDqsResult_(rr);
				return;
			}

			const auto job = dc::account::MakeAccountAuthRequestJob(payload);

			const auto db_result =
				dc::account::AccountAuthDbRepository::ExecuteAuth(
					*db_worker_conns_[shard],
					job);

			dc::account::ApplyAccountAuthRequestResult(db_result, rr);

			PostDqsResult_(rr);
		}
		catch (const std::exception& e) {
			dc::account::SetAccountAuthDbError(rr, e.what());
			PostDqsResult_(rr);
		}
		catch (...) {
			dc::account::SetAccountAuthDbError(rr, "unknown_db_exception");
			PostDqsResult_(rr);
		}
	}

	void AccountLineRuntime::RecycleDqsSlot_(std::uint32_t slot_index)
	{
		std::lock_guard lk(dqs_mtx_);

		if (slot_index >= dqs_slots_.size()) {
			return;
		}

		dqs_slots_[slot_index].reset();
		dqs_empty_.push_back(slot_index);
	}

	void AccountLineRuntime::PostDqsResult_(svr::dqs_result::Result result)
	{
		std::lock_guard lk(dqs_result_mtx_);
		dqs_results_.push_back(std::move(result));
	}

	void AccountLineRuntime::HandleDqsResult_(
		const svr::dqs_result::AccountAuthResult& rr)
	{
		if (!login_handler_) {
			return;
		}

		const bool auth_ok =
			(rr.ok != 0) &&
			(rr.result == svr::dqs::ResultCode::success);

		if (!auth_ok) {
			login_handler_->SendAccountAuthResult(
				0,
				rr.sid,
				rr.serial,
				rr.request_id,
				false,
				0,
				0,
				"",
				"",
				"",
				rr.fail_reason,
				0);
			return;
		}

		std::string world_host;
		std::uint16_t world_port = 0;
		std::uint32_t target_world_server_id = 0;
		std::uint16_t target_world_id = 0;
		std::uint16_t target_channel_id = 0;

		if (!TryGetWorldEndpoint_(world_host, world_port, target_world_server_id, target_world_id, target_channel_id)) {

			login_handler_->SendAccountAuthResult(
				0, rr.sid, rr.serial, rr.request_id,
				false, 0, 0, "", "", "", "world_not_ready", 0);
			return;
		}

		const std::string login_session = rr.login_session;
		const std::string world_token = GenerateWorldToken_();

		PendingWorldTicketUpsert pending{};
		pending.request_id = rr.request_id;
		pending.login_sid = rr.sid;
		pending.login_serial = rr.serial;
		pending.target_world_server_id = target_world_server_id;
		pending.target_world_id = target_world_id;
		pending.target_channel_id = target_channel_id;
		pending.account_id = rr.account_id;
		pending.char_id = rr.char_id;
		pending.login_session = login_session;
		pending.world_token = world_token;
		pending.world_host = world_host;
		pending.world_port = world_port;
		pending.issued_at = std::chrono::steady_clock::now();

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			pending_world_upserts_[pending.world_token] = pending;
		}

		login_handler_->SendAccountAuthResult(
			0,
			rr.sid,
			rr.serial,
			rr.request_id,
			true,
			pending.account_id,
			pending.char_id,
			pending.login_session,
			pending.world_token,
			pending.world_host,
			"",
			pending.world_port);

		spdlog::info(
			"AccountLineRuntime issued world ticket request_id={} account_id={} char_id={} login_sid={} target_world_server_id={} world_id={} channel_id={} endpoint={}:{}",
			rr.request_id,
			rr.account_id,
			rr.char_id,
			rr.sid,
			target_world_server_id,
			target_world_id,
			target_channel_id,
			world_host,
			world_port);
	}

	std::uint16_t AccountLineRuntime::ConsumeWorldAuthTicketBrokered_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token,
		ConsumedWorldTicketAwaitingNotify& out_consumed)
	{
		std::uint32_t sender_world_server_id = 0;
		if (!TryGetRegisteredWorldServerId_(sid, serial, sender_world_server_id)) {
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::WorldServerMismatch);
		}

		std::lock_guard lk(pending_world_upsert_mtx_);

		const auto it = pending_world_upserts_.find(std::string(world_token));
		if (it == pending_world_upserts_.end()) {
			if (consumed_world_enters_awaiting_notify_.find(std::string(world_token)) != consumed_world_enters_awaiting_notify_.end()) {
				return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::ReplayDetected);
			}
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::TokenNotFound);
		}

		if (std::chrono::steady_clock::now() - it->second.issued_at >= dc::k_account_world_expire) {
			pending_world_upserts_.erase(it);
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::Expired);
		}

		if (it->second.login_session != login_session) {
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::LoginSessionMismatch);
		}
		if (it->second.account_id != account_id) {
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::AccountMismatch);
		}
		if (it->second.char_id != char_id) {
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::CharMismatch);
		}
		if (it->second.target_world_server_id != sender_world_server_id) {
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::WorldServerMismatch);
		}

		out_consumed.request_id = it->second.request_id;
		out_consumed.login_sid = it->second.login_sid;
		out_consumed.login_serial = it->second.login_serial;
		out_consumed.target_world_server_id = it->second.target_world_server_id;
		out_consumed.account_id = it->second.account_id;
		out_consumed.char_id = it->second.char_id;
		out_consumed.login_session = it->second.login_session;
		out_consumed.world_token = it->second.world_token;
		out_consumed.world_host = it->second.world_host;
		out_consumed.world_port = it->second.world_port;
		out_consumed.consumed_at = std::chrono::steady_clock::now();

		consumed_world_enters_awaiting_notify_[out_consumed.world_token] = out_consumed;
		pending_world_upserts_.erase(it);
		return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::Ok);
	}

	void AccountLineRuntime::HandleWorldAuthTicketConsume(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		if (!world_handler_) {
			return;
		}

		ConsumedWorldTicketAwaitingNotify consumed{};

		const auto result_code = ConsumeWorldAuthTicketBrokered_(
			sid,
			serial,
			account_id,
			char_id,
			login_session,
			world_token,
			consumed);

		world_handler_->SendWorldAuthTicketConsumeResponse(
			0,
			sid,
			serial,
			request_id,
			result_code,
			consumed.account_id,
			consumed.char_id,
			consumed.login_session,
			consumed.world_token);
	}

	bool AccountLineRuntime::TryFinalizeConsumedWorldEnterSuccess_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token,
		ConsumedWorldTicketAwaitingNotify& out_consumed)
	{
		std::uint32_t sender_world_server_id = 0;
		if (!TryGetRegisteredWorldServerId_(sid, serial, sender_world_server_id)) {
			return false;
		}

		std::lock_guard lk(pending_world_upsert_mtx_);

		const auto it = consumed_world_enters_awaiting_notify_.find(std::string(world_token));
		if (it == consumed_world_enters_awaiting_notify_.end()) {
			return false;
		}

		if (it->second.account_id != account_id ||
			it->second.char_id != char_id ||
			it->second.login_session != login_session ||
			it->second.target_world_server_id != sender_world_server_id)
		{
			return false;
		}

		out_consumed = it->second;
		return true;
	}

	void AccountLineRuntime::OnWorldEnterSuccessNotify(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		ConsumedWorldTicketAwaitingNotify consumed{};
		if (!TryFinalizeConsumedWorldEnterSuccess_(sid, serial, account_id, char_id, login_session, world_token, consumed)) {
			spdlog::warn(
				"AccountLineRuntime dropped world_enter_success_notify without matching consumed ticket. sid={} serial={} account_id={} char_id={} token={}",
				sid,
				serial,
				account_id,
				char_id,
				world_token);
			return;
		}

		if (!login_ready_.load(std::memory_order_acquire) || !login_handler_) {
			spdlog::warn(
				"AccountLineRuntime cannot relay world_enter_success_notify because login is not ready. account_id={} char_id={} token={}",
				account_id,
				char_id,
				world_token);
			return;
		}

		if (!login_handler_->SendWorldEnterSuccessNotify(
			0,
			login_sid_.load(std::memory_order_relaxed),
			login_serial_.load(std::memory_order_relaxed),
			account_id,
			char_id,
			login_session,
			world_token)) {
			spdlog::warn(
				"AccountLineRuntime failed to relay world_enter_success_notify to login. account_id={} char_id={} token={}",
				account_id,
				char_id,
				world_token);
			return;
		}

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			consumed_world_enters_awaiting_notify_.erase(std::string(world_token));
		}

		spdlog::info(
			"AccountLineRuntime relayed world_enter_success_notify to login. account_id={} char_id={} token={}",
			account_id,
			char_id,
			world_token);
	}

	void AccountLineRuntime::ExpirePendingWorldTickets_(std::chrono::steady_clock::time_point now)
	{
		std::lock_guard lk(pending_world_upsert_mtx_);
		for (auto it = pending_world_upserts_.begin(); it != pending_world_upserts_.end();) {
			if (now - it->second.issued_at >= dc::k_pending_world_upserts_expire) {
				it = pending_world_upserts_.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = consumed_world_enters_awaiting_notify_.begin(); it != consumed_world_enters_awaiting_notify_.end();) {
			if (now - it->second.consumed_at >= dc::k_consumed_world_enters_awaiting_notify_expire) {
				spdlog::warn(
					"AccountLineRuntime expired consumed world ticket waiting for success notify. account_id={} char_id={} token={}",
					it->second.account_id,
					it->second.char_id,
					it->second.world_token);
				it = consumed_world_enters_awaiting_notify_.erase(it);
			}
			else {
				++it;
			}
		}
	}

	void AccountLineRuntime::ExpireStaleWorldRoutes_(std::chrono::steady_clock::time_point now)
	{
		std::vector<std::uint32_t> stale_server_ids;
		{
			std::lock_guard lk(world_registry_mtx_);
			for (auto it = worlds_by_sid_.begin(); it != worlds_by_sid_.end();) {
				if (now - it->second.last_heartbeat_at < kWorldRouteHeartbeatTimeout_) {
					++it;
					continue;
				}

				spdlog::warn(
					"AccountLineRuntime expired stale world route. sid={} serial={} server_id={} world_id={} channel_id={} last_load={} zones={}",
					it->second.sid,
					it->second.serial,
					it->second.server_id,
					it->second.world_id,
					it->second.channel_id,
					it->second.load_score,
					it->second.active_zone_count);

				if (it->second.server_id != 0) {
					stale_server_ids.push_back(it->second.server_id);
					const auto idx = world_sid_by_server_id_.find(it->second.server_id);
					if (idx != world_sid_by_server_id_.end() && idx->second == it->first) {
						world_sid_by_server_id_.erase(idx);
					}
				}

				it = worlds_by_sid_.erase(it);
			}
		}

		for (const auto server_id : stale_server_ids) {
			ErasePendingWorldTicketsForServer_(server_id);
		}
	}

	void AccountLineRuntime::DrainDqsResults_()
	{
		std::deque<svr::dqs_result::Result> local;
		{
			std::lock_guard lk(dqs_result_mtx_);
			local.swap(dqs_results_);
		}

		for (auto& r : local) {
			std::visit([this](auto& rr) {
				using T = std::decay_t<decltype(rr)>;
				if constexpr (std::is_same_v<T, svr::dqs_result::AccountAuthResult>) {
					HandleDqsResult_(rr);
				}
			}, r);
		}
	}

	void AccountLineRuntime::HandleAccountAuthRequest(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t request_id,
		std::string_view login_id,
		std::string_view password,
		std::uint64_t selected_char_id)
	{
		if (!login_handler_) {
			return;
		}

		if (login_id.empty()) {
			login_handler_->SendAccountAuthResult(
				0, sid, serial, request_id,
				false, 0, 0, "", "", "", "empty_login_id", 0);
			return;
		}

		if (password.empty()) {
			login_handler_->SendAccountAuthResult(
				0, sid, serial, request_id,
				false, 0, 0, "", "", "", "empty_password", 0);
			return;
		}

		svr::dqs_payload::AccountAuthRequest payload{};
		payload.sid = sid;
		payload.serial = serial;
		payload.request_id = request_id;
		payload.selected_char_id = selected_char_id;

		std::snprintf(
			payload.login_id,
			sizeof(payload.login_id),
			"%.*s",
			static_cast<int>(login_id.size()),
			login_id.data());

		std::snprintf(
			payload.password,
			sizeof(payload.password),
			"%.*s",
			static_cast<int>(password.size()),
			password.data());

		if (!PushAccountAuthDqs_(payload)) {
			login_handler_->SendAccountAuthResult(
				0, sid, serial, request_id,
				false, 0, 0, "", "", "", "db_queue_full", 0);
			return;
		}
	}

	bool AccountLineRuntime::OnRuntimeInit()
	{
		if (!InitDqs_()) {
			spdlog::error("AccountLineRuntime failed to init DQS.");
			return false;
		}

		if (!InitDbWorkers_()) {
			spdlog::error("AccountLineRuntime failed to init DB workers.");
			return false;
		}

		dc::InitHostedLineEntry(
			login_line_,
			0,
			"account-login",
			login_port_,
			false,
			0);

		dc::InitHostedLineEntry(
			world_line_,
			1,
			"account-world",
			world_port_,
			false,
			0);

		login_handler_ = std::make_shared<AccountLoginHandler>(
			[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port) {
			MarkLoginRegistered(sid, serial, server_id, server_name, listen_port);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			MarkLoginDisconnected(sid, serial);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t request_id, std::string_view login_id, std::string_view password, std::uint64_t selected_char_id) {
			HandleAccountAuthRequest(sid, serial, request_id, login_id, password, selected_char_id);
		});

		if (!dc::StartHostedLine(
			login_line_,
			io_,
			login_handler_,
			[](std::uint64_t, std::function<void()> fn) {
			if (fn) {
				fn();
			}
		}))
		{
			spdlog::error("AccountLineRuntime failed to start hosted line. port={}", login_port_);
			ShutdownDbWorkers_();
			return false;
		}

		world_handler_ = std::make_shared<AccountWorldHandler>(
			[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id,
				std::uint16_t world_id, std::uint16_t channel_id,
				std::uint16_t active_zone_count, std::uint16_t load_score, std::uint32_t flags,
				std::string_view server_name, std::string_view public_host, std::uint16_t public_port) {
			MarkWorldRegistered(sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags, server_name, public_host, public_port);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			MarkWorldDisconnected(sid, serial);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t request_id,
				std::uint64_t account_id, std::uint64_t char_id,
				std::string_view login_session, std::string_view world_token) {
			HandleWorldAuthTicketConsume(
				sid,
				serial,
				request_id,
				account_id,
				char_id,
				login_session,
				world_token);
		},
			[this](std::uint32_t sid, std::uint32_t serial,
				std::uint64_t account_id, std::uint64_t char_id,
				std::string_view login_session, std::string_view world_token) {
			OnWorldEnterSuccessNotify(sid, serial, account_id, char_id, login_session, world_token);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id,
				std::uint16_t world_id, std::uint16_t channel_id,
				std::uint16_t active_zone_count, std::uint16_t load_score, std::uint32_t flags) {
			OnWorldRouteHeartbeat(sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags);
		});

		if (!dc::StartHostedLine(
			world_line_,
			io_,
			world_handler_,
			[](std::uint64_t, std::function<void()> fn) {
			if (fn) {
				fn();
			}
		}))
		{
			spdlog::error("AccountLineRuntime failed to start world hosted line. port={}", world_port_);
			ShutdownDbWorkers_();
			return false;
		}

		spdlog::info("AccountLineRuntime started. login_port={}, world_port={}", login_port_, world_port_);
		return true;
	}

	void AccountLineRuntime::OnBeforeIoStop()
	{
		login_ready_.store(false, std::memory_order_release);
		std::lock_guard lk(world_registry_mtx_);
		worlds_by_sid_.clear();
		world_sid_by_server_id_.clear();
	}

	void AccountLineRuntime::OnAfterIoStop()
	{
		login_line_.host.Stop();
		world_line_.host.Stop();
		login_handler_.reset();
		world_handler_.reset();

		login_ready_.store(false, std::memory_order_release);
		login_sid_.store(0, std::memory_order_relaxed);
		login_serial_.store(0, std::memory_order_relaxed);

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			pending_world_upserts_.clear();
			consumed_world_enters_awaiting_notify_.clear();
		}


		{
			std::lock_guard lk(world_registry_mtx_);
			worlds_by_sid_.clear();
			world_sid_by_server_id_.clear();
		}

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			pending_world_upserts_.clear();
			consumed_world_enters_awaiting_notify_.clear();
		}
		ShutdownDbWorkers_();
	}

	void AccountLineRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now)
	{
		DrainDqsResults_();
		ExpirePendingWorldTickets_(now);
		ExpireStaleWorldRoutes_(now);
	}

} // namespace dc
