#include "services/account/runtime/account_line_runtime.h"

#include <algorithm>
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
#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>
#include <inipp/inipp.h>

#include "services/account/db/account_auth_job.h"
#include "server_common/runtime/line_start_helper.h"
#include "services/account/db/account_auth_db_repository.h"
#include "services/account/security/login_session_token.h"
#include "services/account/handler/account_world_handler.h"
#include "services/world/runtime/i_world_runtime.h"
#include "proto/internal/account_world_proto.h"
#include "server_common/log/flow_event_codes.h"
#include "server_common/log/enter_flow_log.h"
#include "server_common/config/runtime_ini_sanity.h"

namespace pt_aw = proto::internal::account_world;

namespace {
	enum class AccountFlowEvent
	{
		LoginCoordinatorReady,
		LoginCoordinatorDisconnected,
		WorldRouteRegistered,
		WorldRouteDisconnected,
		WorldRouteHeartbeatStale,
		WorldRouteHeartbeatMismatch,
		AuthRejected,
		AuthWorldRouteUnavailable,
		WorldTicketIssued,
		WorldTicketConsumeAccepted,
		WorldTicketConsumeRejected,
		WorldEnterNotifyDropped,
		WorldEnterNotifyRelayed,
		WorldTicketAwaitNotifyExpired,
		WorldRouteExpired,
	};

	const char* ToString(AccountFlowEvent e)
	{
		switch (e) {
		case AccountFlowEvent::LoginCoordinatorReady: return dc::logevt::account::kLoginCoordinatorReady;
		case AccountFlowEvent::LoginCoordinatorDisconnected: return dc::logevt::account::kLoginCoordinatorDisconnected;
		case AccountFlowEvent::WorldRouteRegistered: return dc::logevt::account::kWorldRouteRegistered;
		case AccountFlowEvent::WorldRouteDisconnected: return dc::logevt::account::kWorldRouteDisconnected;
		case AccountFlowEvent::WorldRouteHeartbeatStale: return dc::logevt::account::kWorldRouteHeartbeatStale;
		case AccountFlowEvent::WorldRouteHeartbeatMismatch: return dc::logevt::account::kWorldRouteHeartbeatMismatch;
		case AccountFlowEvent::AuthRejected: return dc::logevt::account::kAuthRejected;
		case AccountFlowEvent::AuthWorldRouteUnavailable: return dc::logevt::account::kAuthWorldRouteUnavailable;
		case AccountFlowEvent::WorldTicketIssued: return dc::logevt::account::kWorldTicketIssued;
		case AccountFlowEvent::WorldTicketConsumeAccepted: return dc::logevt::account::kWorldTicketConsumeAccepted;
		case AccountFlowEvent::WorldTicketConsumeRejected: return dc::logevt::account::kWorldTicketConsumeRejected;
		case AccountFlowEvent::WorldEnterNotifyDropped: return dc::logevt::account::kWorldEnterNotifyDropped;
		case AccountFlowEvent::WorldEnterNotifyRelayed: return dc::logevt::account::kWorldEnterNotifyRelayed;
		case AccountFlowEvent::WorldTicketAwaitNotifyExpired: return dc::logevt::account::kWorldTicketAwaitNotifyExpired;
		case AccountFlowEvent::WorldRouteExpired: return dc::logevt::account::kWorldRouteExpired;
		default: return dc::logevt::account::kUnknown;
 		}
	}

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

	void AccountLineRuntime::OnLoginCoordinatorRegistered_(
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
			"[{}] login coordinator ready sid={} serial={} server_id={} server_name={} listen_port={}",
			ToString(AccountFlowEvent::LoginCoordinatorReady),
			sid, serial, server_id, server_name, listen_port);

		if (login_handler_) {
			login_handler_->SendRegisterAck(0, sid, serial, 1, 10, "account", login_port_);
		}
	}

	void AccountLineRuntime::OnLoginCoordinatorDisconnected_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		const auto cur_sid = login_sid_.load(std::memory_order_relaxed);
		const auto cur_serial = login_serial_.load(std::memory_order_relaxed);

		if (cur_sid != 0 && !dc::IsSameSessionKey(cur_sid, cur_serial, sid, serial)) {
			return;
		}

		login_ready_.store(false, std::memory_order_release);
		login_sid_.store(0, std::memory_order_relaxed);
		login_serial_.store(0, std::memory_order_relaxed);

		spdlog::info(
			"[{}] login coordinator disconnected sid={} serial={}",
			ToString(AccountFlowEvent::LoginCoordinatorDisconnected),
			sid,
			serial);
	}

	void AccountLineRuntime::OnWorldRouteRegistered_(
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

			auto prev = worlds_by_sid_.find(sid);
			if (prev != worlds_by_sid_.end()) {
				const auto prev_server_id = prev->second.server_id;
				if (prev_server_id != 0 && prev_server_id != server_id) {
					auto old_idx = world_sid_by_server_id_.find(prev_server_id);
					if (old_idx != world_sid_by_server_id_.end() && old_idx->second == sid) {
						world_sid_by_server_id_.erase(old_idx);
					}
				}
			}

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
			"[{}] world route registered sid={} serial={} server_id={} world_id={} channel_id={} zones={} load_score={} flags={} server_name={} public_host={} public_port={} world_count={}",
			ToString(AccountFlowEvent::WorldRouteRegistered),
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

	void AccountLineRuntime::OnWorldRouteDisconnected_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		std::uint32_t removed_server_id = 0;
		std::size_t world_count = 0;

		{
			std::lock_guard lk(world_registry_mtx_);
			const auto it = worlds_by_sid_.find(sid);
			if (it == worlds_by_sid_.end() || !dc::IsSameSessionKey(it->second.sid, it->second.serial, sid, serial)) {
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
			"[{}] world route disconnected sid={} serial={} server_id={} remaining_worlds={}",
			ToString(AccountFlowEvent::WorldRouteDisconnected), sid,
			serial,
			removed_server_id,
			world_count);
	}

	void AccountLineRuntime::OnWorldRouteHeartbeatReceived_(

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
				"[{}] ignored stale world route heartbeat sid={} serial={} server_id={} world_id={} channel_id={}",
				ToString(AccountFlowEvent::WorldRouteHeartbeatStale), sid,
				serial,
				server_id,
				world_id,
				channel_id);
			return;
		}

		auto& endpoint = it->second;
		if (endpoint.server_id != server_id) {
			spdlog::warn(
				"[{}] ignored world heartbeat with mismatched server_id sid={} serial={} endpoint_server_id={} heartbeat_server_id={}",
				ToString(AccountFlowEvent::WorldRouteHeartbeatMismatch),
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


	bool AccountLineRuntime::BuildWorldSummaryList_(
		std::vector<proto::internal::login_account::WorldSummary>& out_worlds) const
	{
		std::lock_guard lk(world_registry_mtx_);
		out_worlds.clear();
		for (const auto& [_, endpoint] : worlds_by_sid_) {
			if (!IsWorldSelectable_(endpoint)) {
				continue;
			}
			proto::internal::login_account::WorldSummary summary{};
			summary.world_id = endpoint.world_id;
			summary.channel_id = endpoint.channel_id;
			summary.active_zone_count = endpoint.active_zone_count;
			summary.load_score = endpoint.load_score;
			summary.public_port = endpoint.public_port;
			summary.flags = endpoint.flags;
			std::snprintf(summary.server_name, sizeof(summary.server_name), "%s", endpoint.server_name.c_str());
			std::snprintf(summary.public_host, sizeof(summary.public_host), "%s", endpoint.public_host.c_str());
			out_worlds.push_back(summary);
		}

		std::sort(out_worlds.begin(), out_worlds.end(), [](const auto& a, const auto& b) {
			if (a.world_id != b.world_id) return a.world_id < b.world_id;
			if (a.channel_id != b.channel_id) return a.channel_id < b.channel_id;
			if (a.load_score != b.load_score) return a.load_score < b.load_score;
			return a.active_zone_count > b.active_zone_count;
		});
		return !out_worlds.empty();
	}

	bool AccountLineRuntime::TryResolveSelectedWorldRoute_(
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::string& out_host,
		std::uint16_t& out_port,
		std::uint32_t& out_server_id) const
	{
		std::lock_guard lk(world_registry_mtx_);
		for (const auto& [_, endpoint] : worlds_by_sid_) {
			if (!IsWorldSelectable_(endpoint)) {
				continue;
			}
			if (endpoint.world_id != world_id || endpoint.channel_id != channel_id) {
				continue;
			}
			out_host = endpoint.public_host;
			out_port = endpoint.public_port;
			out_server_id = endpoint.server_id;
			return true;
		}
		return false;
	}

	bool AccountLineRuntime::TrySelectWorldRouteEndpoint_(
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

	bool AccountLineRuntime::TryResolveRegisteredWorldServerId_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t& out_server_id) const
	{
		std::lock_guard lk(world_registry_mtx_);
		const auto it = worlds_by_sid_.find(sid);
		if (it == worlds_by_sid_.end() || !dc::IsSameSessionKey(it->second.sid, it->second.serial, sid, serial)) {
			return false;
		}

		out_server_id = it->second.server_id;
		return out_server_id != 0;
	}


	bool AccountLineRuntime::TryResolveWorldRouteSessionByServerId_(
		std::uint32_t world_server_id,
		std::uint32_t& out_sid,
		std::uint32_t& out_serial) const
	{
		std::lock_guard lk(world_registry_mtx_);
		const auto idx = world_sid_by_server_id_.find(world_server_id);
		if (idx == world_sid_by_server_id_.end()) {
			return false;
		}
		const auto it = worlds_by_sid_.find(idx->second);
		if (it == worlds_by_sid_.end()) {
			return false;
		}
		out_sid = it->second.sid;
		out_serial = it->second.serial;
		return dc::IsValidSessionKey(out_sid, out_serial);
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

	std::uint32_t AccountLineRuntime::RouteAccountShard_(
		const svr::dqs_payload::AccountCharacterListRequest& payload) const
	{
		if (db_shard_count_ <= 1) {
			return 0;
		}
		return static_cast<std::uint32_t>(payload.account_id % db_shard_count_);
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

	bool AccountLineRuntime::PushAccountCharacterListDqs_(
		const svr::dqs_payload::AccountCharacterListRequest& payload)
	{
		static_assert(sizeof(svr::dqs_payload::AccountCharacterListRequest) <= svr::dqs::DqsSlot::max_data_size);

		std::uint32_t idx = 0;
		{
			std::lock_guard lk(dqs_mtx_);
			if (dqs_empty_.empty()) {
				dqs_drop_count_.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			idx = dqs_empty_.front();
			dqs_empty_.pop_front();

			auto& slot = dqs_slots_[idx];
			slot.reset();
			slot.process_code = static_cast<std::uint8_t>(svr::dqs::ProcessCode::account);
			slot.qry_case = static_cast<std::uint8_t>(svr::dqs::QueryCase::account_character_list);
			slot.result = svr::dqs::ResultCode::success;
			slot.in_use = true;
			slot.done = false;
			slot.data_size = static_cast<std::uint16_t>(sizeof(payload));
			std::memcpy(slot.data.data(), &payload, sizeof(payload));
		}

		if (!db_shards_) {
			RecycleDqsSlot_(idx);
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

		if (pc != svr::dqs::ProcessCode::account) {
			slot.result = svr::dqs::ResultCode::invalid_data;
			return;
		}

		switch (qc) {
		case svr::dqs::QueryCase::account_auth:
			{
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
					const auto db_result = dc::account::AccountAuthDbRepository::ExecuteAuth(*db_worker_conns_[shard], job);
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
				return;
			}
		case svr::dqs::QueryCase::account_character_list:
			{
				if (slot.data_size < sizeof(svr::dqs_payload::AccountCharacterListRequest)) {
					slot.result = svr::dqs::ResultCode::invalid_data;
					return;
				}

				svr::dqs_payload::AccountCharacterListRequest payload{};
				std::memcpy(&payload, slot.data.data(), sizeof(payload));
				auto rr = dc::account::MakePendingAccountCharacterListResult(payload);

				try {
					const auto shard = RouteAccountShard_(payload);
					if (shard >= db_worker_conns_.size() || !db_worker_conns_[shard]) {
						dc::account::SetAccountCharacterListDbError(rr, "db_conn_not_ready");
						PostDqsResult_(rr);
						return;
					}

					const auto job = dc::account::MakeAccountCharacterListRequestJob(payload);
					const auto db_result = dc::account::AccountAuthDbRepository::ExecuteCharacterList(*db_worker_conns_[shard], job);
					dc::account::ApplyAccountCharacterListRequestResult(db_result, rr);
					PostDqsResult_(rr);
				}
				catch (const std::exception& e) {
					dc::account::SetAccountCharacterListDbError(rr, e.what());
					PostDqsResult_(rr);
				}
				catch (...) {
					dc::account::SetAccountCharacterListDbError(rr, "unknown_db_exception");
					PostDqsResult_(rr);
				}
				return;
			}
		default:
			slot.result = svr::dqs::ResultCode::invalid_data;
			return;
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
			spdlog::warn(
				"[{}] auth rejected request_id={} sid={} serial={} fail_reason={} result={}",
				ToString(AccountFlowEvent::AuthRejected),
				rr.request_id,
				rr.sid,
				rr.serial,
				rr.fail_reason,
				static_cast<int>(rr.result));
			login_handler_->SendAccountAuthResult(
				0,
				rr.sid,
				rr.serial,
				rr.trace_id,
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
		const std::string login_session = rr.login_session;
		{
			std::lock_guard lk(login_character_sessions_mtx_);
			auto& session = login_character_sessions_[login_session];
			session.trace_id = rr.trace_id;
			session.login_sid = rr.sid;
			session.login_serial = rr.serial;
			session.account_id = rr.account_id;
			session.selected_char_id = 0;
			session.selected_world_id = 0;
			session.selected_channel_id = 0;
			session.selected_world_server_id = 0;
			session.selected_world_host.clear();
			session.selected_world_port = 0;
			session.login_session = login_session;
			session.cached_characters.clear();
			session.updated_at = std::chrono::steady_clock::now();
		}

		login_handler_->SendAccountAuthResult(
			0,
			rr.sid,
			rr.serial,
			rr.trace_id,
			rr.request_id,
			true,
			rr.account_id,
			0,
			login_session,
			"",
			"",
			"",
			0);
	}

	void AccountLineRuntime::HandleDqsResult_(
		const svr::dqs_result::AccountCharacterListResult& rr)
	{
		if (!login_handler_) {
			return;
		}

		if (rr.ok == 0 || rr.result != svr::dqs::ResultCode::success) {
			login_handler_->SendCharacterListResult(
				0,
				rr.sid,
				rr.serial,
				rr.trace_id,
				rr.request_id,
				false,
				rr.account_id,
				0,
				nullptr,
				rr.fail_reason);
			return;
		}

		{
			std::lock_guard lk(login_character_sessions_mtx_);
			for (auto& [_, session] : login_character_sessions_) {
				if (session.account_id == rr.account_id && session.trace_id == rr.trace_id) {
					session.cached_characters.clear();
					for (std::size_t i = 0; i < std::min<std::size_t>(rr.count, dc::k_character_list_max_count); ++i) {
						session.cached_characters.push_back(proto::internal::login_account::CharacterSummary{
							rr.characters[i].char_id,
							{},
							rr.characters[i].level,
							rr.characters[i].job,
							rr.characters[i].appearance_code,
							rr.characters[i].last_login_at_epoch_sec
						});
						std::snprintf(
							session.cached_characters.back().char_name,
							sizeof(session.cached_characters.back().char_name),
							"%s",
							rr.characters[i].char_name);
					}
					session.updated_at = std::chrono::steady_clock::now();
					break;
				}
			}
		}

		pt_la::CharacterSummary characters[dc::k_character_list_max_count]{};
		for (std::size_t i = 0; i < std::min<std::size_t>(rr.count, dc::k_character_list_max_count); ++i) {
			characters[i].char_id = rr.characters[i].char_id;
			std::snprintf(characters[i].char_name, sizeof(characters[i].char_name), "%s", rr.characters[i].char_name);
			characters[i].level = rr.characters[i].level;
			characters[i].job = rr.characters[i].job;
			characters[i].appearance_code = rr.characters[i].appearance_code;
			characters[i].last_login_at_epoch_sec = rr.characters[i].last_login_at_epoch_sec;
		}

		login_handler_->SendCharacterListResult(
			0,
			rr.sid,
			rr.serial,
			rr.trace_id,
			rr.request_id,
			true,
			rr.account_id,
			rr.count,
			characters,
			"");
	}


	void AccountLineRuntime::HandleWorldCharacterListResponse_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::uint16_t world_id,
		std::uint16_t count,
		bool ok,
		std::string_view login_session,
		const pt_aw::WorldCharacterSummary* characters,
		std::string_view fail_reason)
	{
		if (!login_handler_) {
			return;
		}

		std::uint32_t sender_world_server_id = 0;
		if (!TryResolveRegisteredWorldServerId_(sid, serial, sender_world_server_id)) {
			return;
		}

		std::uint32_t login_sid = 0;
		std::uint32_t login_serial = 0;
		{
			std::lock_guard lk(login_character_sessions_mtx_);
			const auto it = login_character_sessions_.find(std::string(login_session));
			if (it == login_character_sessions_.end() ||
				it->second.account_id != account_id ||
				it->second.selected_world_server_id != sender_world_server_id ||
				it->second.selected_world_id != world_id) {
				return;
			}
			login_sid = it->second.login_sid;
			login_serial = it->second.login_serial;

			it->second.cached_characters.clear();
			if (ok && characters != nullptr) {
				for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_character_list_max_count); ++i) {
					proto::internal::login_account::CharacterSummary entry{};
					entry.char_id = characters[i].char_id;
					std::snprintf(entry.char_name, sizeof(entry.char_name), "%s", characters[i].char_name);
					entry.level = characters[i].level;
					entry.job = characters[i].job;
					entry.appearance_code = characters[i].appearance_code;
					entry.last_login_at_epoch_sec = characters[i].last_login_at_epoch_sec;
					it->second.cached_characters.push_back(entry);
				}
			}
			it->second.updated_at = std::chrono::steady_clock::now();
		}

		if (!ok) {
			login_handler_->SendCharacterListResult(0, login_sid, login_serial, trace_id, request_id, false, account_id, 0, nullptr, fail_reason);
			return;
		}

		pt_la::CharacterSummary out[dc::k_character_list_max_count]{};
		for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_character_list_max_count); ++i) {
			out[i].char_id = characters[i].char_id;
			std::snprintf(out[i].char_name, sizeof(out[i].char_name), "%s", characters[i].char_name);
			out[i].level = characters[i].level;
			out[i].job = characters[i].job;
			out[i].appearance_code = characters[i].appearance_code;
			out[i].last_login_at_epoch_sec = characters[i].last_login_at_epoch_sec;
		}

		login_handler_->SendCharacterListResult(
			0,
			login_sid,
			login_serial,
			trace_id,
			request_id,
			true,
			account_id,
			count,
			out,
			"");
	}

	std::uint16_t AccountLineRuntime::ConsumeIssuedWorldTicket_(std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t account_id,
		std::string_view login_session,
		std::string_view world_token,
		ConsumedWorldTicketAwaitingNotify& out_consumed)
	{
		std::uint32_t sender_world_server_id = 0;
		if (!TryResolveRegisteredWorldServerId_(sid, serial, sender_world_server_id)) {
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
		if (it->second.target_world_server_id != sender_world_server_id) {
			return static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::WorldServerMismatch);
		}

		out_consumed.request_id = it->second.request_id;
		out_consumed.trace_id = it->second.trace_id != 0 ? it->second.trace_id : trace_id;
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

	void AccountLineRuntime::HandleWorldTicketConsumeRequest_(std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		if (!world_handler_) {
			return;
		}

		ConsumedWorldTicketAwaitingNotify consumed{};

		const auto result_code = ConsumeIssuedWorldTicket_(
			sid,
			serial,
			trace_id,
			account_id,
			login_session,
			world_token,
			consumed);

		if (result_code == static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::Ok)) {
			spdlog::info(
				"[{}] world ticket consume accepted request_id={} sid={} serial={} account_id={} char_id={} token={}",
				ToString(AccountFlowEvent::WorldTicketConsumeAccepted),
				request_id,
				sid,
				serial,
				consumed.account_id,
				consumed.char_id,
				consumed.world_token);
			dc::enterlog::LogEnterFlow(
				spdlog::level::info,
				dc::enterlog::EnterStage::WorldTokenConsumeResult,
				{ consumed.trace_id, consumed.account_id, consumed.char_id, sid, serial, consumed.login_session, consumed.world_token },
				{},
				"account_consume_ok");
		}
		else {
			spdlog::warn(
				"[{}] world ticket consume rejected request_id={} sid={} serial={} account_id={} char_id={} token={} result_code={}",
				ToString(AccountFlowEvent::WorldTicketConsumeRejected),
				request_id,
				sid,
				serial,
				account_id,
				0,
				world_token,
				result_code);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::WorldTokenConsumeResult,
				{ trace_id, account_id, 0, sid, serial, login_session, world_token },
				"consume_rejected");
		}

		world_handler_->SendWorldAuthTicketConsumeResponse(
			0,
			sid,
			serial,
			result_code == static_cast<std::uint16_t>(svr::ConsumePendingWorldAuthTicketResultKind::Ok) ? consumed.trace_id : trace_id,
			request_id,
			result_code,
			consumed.account_id,
			consumed.char_id,
			consumed.login_session,
			consumed.world_token);
	}

	bool AccountLineRuntime::TryMatchConsumedWorldEnterSuccessNotify_(

		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token,
		ConsumedWorldTicketAwaitingNotify& out_consumed)
	{
		std::uint32_t sender_world_server_id = 0;
		if (!TryResolveRegisteredWorldServerId_(sid, serial, sender_world_server_id)) {
			return false;
		}

		std::lock_guard lk(pending_world_upsert_mtx_);

		const auto it = consumed_world_enters_awaiting_notify_.find(std::string(world_token));
		if (it == consumed_world_enters_awaiting_notify_.end()) {
			return false;
		}

		if ((trace_id != 0 && it->second.trace_id != trace_id) ||
			it->second.account_id != account_id ||
			it->second.char_id != char_id ||
			it->second.login_session != login_session ||
			it->second.target_world_server_id != sender_world_server_id)
		{
			return false;
		}

		out_consumed = it->second;
		return true;
	}

	void AccountLineRuntime::HandleWorldEnterSuccessNotify_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		ConsumedWorldTicketAwaitingNotify consumed{};
		if (!TryMatchConsumedWorldEnterSuccessNotify_(sid, serial, trace_id, account_id, char_id, login_session, world_token, consumed)) {
			spdlog::warn(
				"[{}] dropped world_enter_success_notify without matching consumed ticket sid={} serial={} account_id={} char_id={} token={}",
				ToString(AccountFlowEvent::WorldEnterNotifyDropped),
				sid,
				serial,
				account_id,
				char_id,
				world_token);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ trace_id, account_id, char_id, sid, serial, login_session, world_token },
				"success_notify_without_ticket");
			return;
		}

		if (!login_ready_.load(std::memory_order_acquire) || !login_handler_) {
			spdlog::warn(
				"[{}] cannot relay world_enter_success_notify because login is not ready account_id={} char_id={} token={}",
				ToString(AccountFlowEvent::WorldEnterNotifyDropped),
				account_id,
				char_id,
				world_token);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ consumed.trace_id, account_id, char_id, sid, serial, login_session, world_token },
				"login_not_ready_for_success_notify");
			return;
		}

		if (!login_handler_->SendWorldEnterSuccessNotify(
			0,
			login_sid_.load(std::memory_order_relaxed),
			login_serial_.load(std::memory_order_relaxed),
			consumed.trace_id,
			account_id,
			char_id,
			login_session,
			world_token)) {
			spdlog::warn(
				"[{}] failed to relay world_enter_success_notify to login account_id={} char_id={} token={}",
				ToString(AccountFlowEvent::WorldEnterNotifyDropped),
				account_id,
				char_id,
				world_token);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ consumed.trace_id, account_id, char_id, sid, serial, login_session, world_token },
				"relay_success_notify_failed");
			return;
		}

		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			consumed_world_enters_awaiting_notify_.erase(std::string(world_token));
		}

		spdlog::info(
			"[{}] relayed world_enter_success_notify to login account_id={} char_id={} token={}",
			ToString(AccountFlowEvent::WorldEnterNotifyRelayed),
			account_id,
			char_id,
			world_token);
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldEnterSuccessNotifySent,
			{ consumed.trace_id, account_id, char_id, sid, serial, login_session, world_token },
			{},
			"account_relayed_to_login");
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
					"[{}] expired consumed world ticket waiting for success notify account_id={} char_id={} token={}",
					ToString(AccountFlowEvent::WorldTicketAwaitNotifyExpired),
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
		std::vector<std::pair<std::uint32_t, std::uint32_t>> stale_sessions;
		{
			std::lock_guard lk(world_registry_mtx_);
			for (auto it = worlds_by_sid_.begin(); it != worlds_by_sid_.end();) {
				if (now - it->second.last_heartbeat_at < kWorldRouteHeartbeatTimeout_) {
					++it;
					continue;
				}

				const auto expired_sid = it->second.sid;
				const auto expired_serial = it->second.serial;

				spdlog::warn(
					"[{}] expired stale world route sid={} serial={} server_id={} world_id={} channel_id={} last_load={} zones={}",
					ToString(AccountFlowEvent::WorldRouteExpired),
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
				// stale route는 heartbeat만으로는 다시 살아나지 않으므로
				// 실제 세션 close를 유도해서 world 쪽 reconnect + hello/register 재수행이 일어나게 한다.
				stale_sessions.emplace_back(expired_sid, expired_serial);

				it = worlds_by_sid_.erase(it);
			}
		}

		for (const auto server_id : stale_server_ids) {
			ErasePendingWorldTicketsForServer_(server_id);
		}

		// 주의:
		// Close()는 disconnect callback -> OnWorldRouteDisconnected_()를 다시 유발할 수 있으므로
		// 반드시 registry lock 밖에서 호출한다.
		if (world_handler_) {
			for (const auto& [sid, serial] : stale_sessions) {
				spdlog::warn(
					"[{}] force closing stale world session sid={} serial={} after route expiration",
					ToString(AccountFlowEvent::WorldRouteExpired),
					sid,
					serial);

				world_handler_->Close(0, sid, serial, false);
			}
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
				else if constexpr (std::is_same_v<T, svr::dqs_result::AccountCharacterListResult>) {
					HandleDqsResult_(rr);
				}
			}, r);
		}
	}

	void AccountLineRuntime::HandleLoginAuthRequest_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::string_view login_id,
		std::string_view password)
	{
		if (!login_handler_) {
			return;
		}

		if (login_id.empty()) {
			login_handler_->SendAccountAuthResult(
				0, sid, serial, trace_id, request_id,
				false, 0, 0, "", "", "", "empty_login_id", 0);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ trace_id, 0, 0, sid, serial, {}, {} },
				"empty_login_id");
			return;
		}

		if (password.empty()) {
			login_handler_->SendAccountAuthResult(
				0, sid, serial, trace_id, request_id,
				false, 0, 0, "", "", "", "empty_password", 0);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ trace_id, 0, 0, sid, serial, {}, {} },
				"empty_password");
			return;
		}

		svr::dqs_payload::AccountAuthRequest payload{};
		payload.sid = sid;
		payload.serial = serial;
		payload.trace_id = trace_id;
		payload.request_id = request_id;

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

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::AccountAuthRequestReceived,
			{ trace_id, 0, 0, sid, serial, {}, {} });

		if (!PushAccountAuthDqs_(payload)) {
			login_handler_->SendAccountAuthResult(
				0, sid, serial, trace_id, request_id,
				false, 0, 0, "", "", "", "db_queue_full", 0);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ trace_id, 0, 0, sid, serial, {}, {} },
				"db_queue_full");
			return;
		}
	}

	void AccountLineRuntime::HandleWorldListRequest_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::string_view login_session)
	{
		std::vector<pt_la::WorldSummary> worlds;
		{
			std::lock_guard lk(login_character_sessions_mtx_);
			const auto it = login_character_sessions_.find(std::string(login_session));
			if (it == login_character_sessions_.end() || it->second.account_id != account_id) {
				login_handler_->SendWorldListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "invalid_login_session");
				return;
			}
		}

		BuildWorldSummaryList_(worlds);
		login_handler_->SendWorldListResult(
			0,
			sid,
			serial,
			trace_id,
			request_id,
			!worlds.empty(),
			account_id,
			static_cast<std::uint16_t>(std::min<std::size_t>(worlds.size(), dc::k_world_list_max_count)),
			worlds.empty() ? nullptr : worlds.data(),
			worlds.empty() ? "world_not_ready" : "");
	}

	void AccountLineRuntime::HandleWorldSelectRequest_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::string_view login_session)
	{
		std::string world_host;
		std::uint16_t world_port = 0;
		std::uint32_t world_server_id = 0;

		{
			std::lock_guard lk(login_character_sessions_mtx_);
			const auto it = login_character_sessions_.find(std::string(login_session));
			if (it == login_character_sessions_.end() || it->second.account_id != account_id) {
				login_handler_->SendWorldSelectResult(0, sid, serial, trace_id, request_id, false, account_id, world_id, channel_id, 0, login_session, "", "invalid_login_session", 0);
				return;
			}
		}

		if (!TryResolveSelectedWorldRoute_(world_id, channel_id, world_host, world_port, world_server_id)) {
			login_handler_->SendWorldSelectResult(0, sid, serial, trace_id, request_id, false, account_id, world_id, channel_id, 0, login_session, "", "world_not_found", 0);
			return;
		}

		{
			std::lock_guard lk(login_character_sessions_mtx_);
			auto it = login_character_sessions_.find(std::string(login_session));
			if (it == login_character_sessions_.end() || it->second.account_id != account_id) {
				login_handler_->SendWorldSelectResult(0, sid, serial, trace_id, request_id, false, account_id, world_id, channel_id, 0, login_session, "", "invalid_login_session", 0);
				return;
			}
			it->second.selected_world_id = world_id;
			it->second.selected_channel_id = channel_id;
			it->second.selected_world_server_id = world_server_id;
			it->second.selected_world_host = world_host;
			it->second.selected_world_port = world_port;
			it->second.updated_at = std::chrono::steady_clock::now();
		}

		login_handler_->SendWorldSelectResult(0, sid, serial, trace_id, request_id, true, account_id, world_id, channel_id, world_server_id, login_session, world_host, "", world_port);
	}

	void AccountLineRuntime::HandleCharacterListRequest_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::uint16_t world_id,
		std::string_view login_session)
	{
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::CharacterListRequestReceived,
			{ trace_id, account_id, 0, sid, serial, login_session, {} });

		{
			std::lock_guard lk(login_character_sessions_mtx_);
			const auto it = login_character_sessions_.find(std::string(login_session));
			if (it == login_character_sessions_.end() || it->second.account_id != account_id) {
				login_handler_->SendCharacterListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "invalid_login_session");
				return;
			}
			if (it->second.selected_world_server_id == 0 || it->second.selected_world_id == 0) {
				login_handler_->SendCharacterListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "world_not_selected");
				return;
			}
		}

		std::uint32_t world_sid = 0;
		std::uint32_t world_serial = 0;
		std::uint32_t selected_world_server_id = 0;
		{
			std::lock_guard lk(login_character_sessions_mtx_);
			const auto it = login_character_sessions_.find(std::string(login_session));
			if (it == login_character_sessions_.end() || it->second.account_id != account_id) {
				login_handler_->SendCharacterListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "invalid_login_session");
				return;
			}
			selected_world_server_id = it->second.selected_world_server_id;
            if (world_id == 0) {
                world_id = it->second.selected_world_id;
            } else if (world_id != it->second.selected_world_id) {
                login_handler_->SendCharacterListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "world_selection_mismatch");
                return;
            }
		}

		if (!world_handler_ || !TryResolveWorldRouteSessionByServerId_(selected_world_server_id, world_sid, world_serial)) {
			login_handler_->SendCharacterListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "world_route_not_ready");
			return;
		}

		if (!world_handler_->SendWorldCharacterListRequest(
			0,
			world_sid,
			world_serial,
			trace_id,
			request_id,
			account_id,
			world_id,
			login_session)) {
			login_handler_->SendCharacterListResult(0, sid, serial, trace_id, request_id, false, account_id, 0, nullptr, "world_route_send_failed");
		}
	}

	void AccountLineRuntime::HandleCharacterSelectRequest_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session)
	{
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::CharacterSelectRequestReceived,
			{ trace_id, account_id, char_id, sid, serial, login_session, {} });

		LoginCharacterSession session{};
		bool found = false;
		{
			std::lock_guard lk(login_character_sessions_mtx_);
			const auto it = login_character_sessions_.find(std::string(login_session));
			if (it != login_character_sessions_.end() && it->second.account_id == account_id) {
				session = it->second;
				found = true;
			}
		}
		if (!found) {
			login_handler_->SendCharacterSelectResult(0, sid, serial, trace_id, request_id, false, account_id, 0, login_session, "", "", "invalid_login_session", 0);
			return;
		}

		const auto selected_it = std::find_if(
			session.cached_characters.begin(),
			session.cached_characters.end(),
			[char_id](const proto::internal::login_account::CharacterSummary& e) {
				return e.char_id == char_id;
			});
		if (selected_it == session.cached_characters.end()) {
			login_handler_->SendCharacterSelectResult(0, sid, serial, trace_id, request_id, false, account_id, char_id, login_session, "", "", "character_not_found", 0);
			return;
		}

		if (session.selected_world_server_id == 0 || session.selected_world_id == 0) {
			login_handler_->SendCharacterSelectResult(0, sid, serial, trace_id, request_id, false, account_id, char_id, login_session, "", "", "world_not_selected", 0);
			return;
		}

		std::string world_host = session.selected_world_host;
		std::uint16_t world_port = session.selected_world_port;
		std::uint32_t target_world_server_id = session.selected_world_server_id;
		std::uint16_t target_world_id = session.selected_world_id;
		std::uint16_t target_channel_id = session.selected_channel_id;
		if (world_host.empty() || world_port == 0 || target_world_server_id == 0) {
			if (!TryResolveSelectedWorldRoute_(target_world_id, target_channel_id, world_host, world_port, target_world_server_id)) {
				login_handler_->SendCharacterSelectResult(0, sid, serial, trace_id, request_id, false, account_id, char_id, login_session, "", "", "world_not_ready", 0);
				return;
			}
		}

		const std::string world_token = GenerateWorldToken_();
		PendingWorldTicketUpsert pending{};
		pending.trace_id = trace_id;
		pending.request_id = request_id;
		pending.login_sid = sid;
		pending.login_serial = serial;
		pending.target_world_server_id = target_world_server_id;
		pending.target_world_id = target_world_id;
		pending.target_channel_id = target_channel_id;
		pending.account_id = account_id;
		pending.char_id = char_id;
		pending.login_session.assign(login_session.data(), login_session.size());
		pending.world_token = world_token;
		pending.world_host = world_host;
		pending.world_port = world_port;
		pending.issued_at = std::chrono::steady_clock::now();
		{
			std::lock_guard lk(pending_world_upsert_mtx_);
			pending_world_upserts_[pending.world_token] = pending;
		}
		{
			std::lock_guard lk(login_character_sessions_mtx_);
			auto it = login_character_sessions_.find(std::string(login_session));
			if (it != login_character_sessions_.end()) {
				it->second.selected_char_id = char_id;
				it->second.trace_id = trace_id;
				it->second.updated_at = std::chrono::steady_clock::now();
			}
		}

		login_handler_->SendCharacterSelectResult(
			0, sid, serial, trace_id, request_id, true, account_id, char_id,
			login_session, world_token, world_host, "", world_port);
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldTicketIssued,
			{ trace_id, account_id, char_id, sid, serial, login_session, world_token },
			{},
			"character_select_issued_world_ticket");
	}
	bool AccountLineRuntime::LoadIniFile()
	{
		namespace fs = std::filesystem;
		const fs::path ini_path = fs::current_path() / "Initialize" / "AccountSystem.ini";

		std::ifstream is(ini_path, std::ios::in | std::ios::binary);
		if (!is) {
			spdlog::error("INI open failed: {}", ini_path.string());
			return false;
		}

		inipp::Ini<char> ini;
		ini.parse(is);

		// ------------------------------------------------------------
			// 0) local defaults (최종 fallback)
			// ------------------------------------------------------------
		bool config_fail_fast = false;
		std::string parse_error;
		std::string parse_warn;

		auto parse_int_field = [&](const std::string& key, const std::string& raw, int& target) -> bool {
			parse_error.clear();
			parse_warn.clear();
			if (!dc::cfg::ParseIntOrKeep(key.c_str(), raw, target, config_fail_fast, &parse_error, &parse_warn)) {
				spdlog::error("[config] {}", parse_error);
				return false;
			}
			if (!parse_warn.empty()) {
				spdlog::warn("[config] {}", parse_warn);
			}
			return true;
		};
		auto parse_u32_field = [&](const std::string& key, const std::string& raw, std::uint32_t& target) -> bool {
			parse_error.clear();
			parse_warn.clear();
			if (!dc::cfg::ParseU32OrKeep(key.c_str(), raw, target, config_fail_fast, &parse_error, &parse_warn)) {
				spdlog::error("[config] {}", parse_error);
				return false;
			}
			if (!parse_warn.empty()) {
				spdlog::warn("[config] {}", parse_warn);
			}
			return true;
		};

		// inipp는 기본적으로 trim/escape 처리가 있음
		// ini.default_section(ini.sections[""]);

		// [LOGDB_INFO]
		db_ip_ = ini.sections["Database"]["Address"];
		db_dns_ = ini.sections["Database"]["DNS"];

		// [REDIS]
		{
			auto v = ini.sections["REDIS"]["Host"];
			if (!v.empty()) redis_host_ = v;
		}
		{
			auto v = ini.sections["REDIS"]["Port"];
			if (!parse_int_field("REDIS.Port", v, redis_port_)) return false;
		}
		{
			auto v = ini.sections["REDIS"]["DB"];
			if (!parse_int_field("REDIS.DB", v, redis_db_)) return false;
		}
		redis_password_ = ini.sections["REDIS"]["Password"];
		{
			auto v = ini.sections["REDIS"]["Prefix"];
			if (!v.empty()) redis_prefix_ = v;
		}

		// ✅ Redis shard + WAIT 옵션
		{
			auto v = ini.sections["REDIS"]["SHARD_COUNT"];
			if (!parse_u32_field("REDIS.SHARD_COUNT", v, redis_shard_count_)) return false;
		}
		{
			auto v = ini.sections["REDIS"]["WAIT_REPLICAS"];
			if (!parse_int_field("REDIS.WAIT_REPLICAS", v, redis_wait_replicas_)) return false;
		}
		{
			auto v = ini.sections["REDIS"]["WAIT_TIMEOUT_MS"];
			if (!parse_int_field("REDIS.WAIT_TIMEOUT_MS", v, redis_wait_timeout_ms_)) return false;
		}

		// ✅ write-behind 튜닝
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_INTERVAL_SEC"];
			if (!parse_int_field("WRITE_BEHIND.FLUSH_INTERVAL_SEC", v, flush_interval_sec_)) return false;
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_BATCH_IMMEDIATE"];
			if (!parse_u32_field("WRITE_BEHIND.FLUSH_BATCH_IMMEDIATE", v, flush_batch_immediate_)) return false;
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_BATCH_NORMAL"];
			if (!parse_u32_field("WRITE_BEHIND.FLUSH_BATCH_NORMAL", v, flush_batch_normal_)) return false;
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["CHAR_TTL_SEC"];
			if (!parse_int_field("WRITE_BEHIND.CHAR_TTL_SEC", v, char_ttl_sec_)) return false;
		}
		{
			auto v = ini.sections["SESSION"]["RECONNECT_GRACE_CLOSE_DELAY_MS"];
			if (!parse_int_field("SESSION.RECONNECT_GRACE_CLOSE_DELAY_MS", v, reconnect_grace_close_delay_ms_)) return false;
		}

		// ✅ DB/DQS 샤딩 관련
		{
			auto v = ini.sections["DB_WORK"]["DB_POOL_SIZE_PER_WORLD"];
			if (!parse_int_field("DB_WORK.DB_POOL_SIZE_PER_WORLD", v, db_pool_size_per_world_)) return false;
		}
		{
			auto v = ini.sections["DB_WORK"]["DB_SHARD_COUNT"];
			if (!parse_u32_field("DB_WORK.DB_SHARD_COUNT", v, db_shard_count_)) return false;
		}

		// [World]

		auto& w = world_info_;

		// Name은 한글 가능 → UTF-8 그대로 std::string에 보관하는 게 베스트
		w.name_utf8 = ini.sections["World"]["Name"];
		w.address = ini.sections["World"]["Address"];
		w.dsn = ini.sections["World"]["DSN")];
		w.dbname = ini.sections["World"]["DBName"];

		{
			auto port = ini.sections["World"]["Port"];
			int parsed_port = 0;
			if (!parse_int_field(std::string("World.") + "Port", port, parsed_port)) return false;
			w.port = parsed_port;
		}

		{
			auto idx = ini.sections["World"]["WorldIdx"];
			int parsed_idx = 0;
			if (!parse_int_field(std::string("World.") + "WorldIdx", idx, parsed_idx)) return false;
			w.world_idx = parsed_idx;
		}

		// [NET_WORK]
		world_to_log_recv_buffer_size_ = 10'000'000;
		{
			auto v = ini.sections["NET_WORK"]["WORLD_TO_LOG_RECV_BUFFER_SIZE"];
			if (!parse_u32_field("NET_WORK.WORLD_TO_LOG_RECV_BUFFER_SIZE", v, world_to_log_recv_buffer_size_)) return false;
		}
		// ✅ io_context run() 스레드 개수(기본 1)
		{
			auto v = ini.sections["NET_WORK"]["IO_THREAD_COUNT"];
			if (!parse_int_field("NET_WORK.IO_THREAD_COUNT", v, io_thread_count_)) return false;
		}

		// ✅ 로직(Actor) 스레드 개수(기본 1)
		{
			auto v = ini.sections["NET_WORK"]["LOGIC_THREAD_COUNT"];
			if (!parse_int_field("NET_WORK.LOGIC_THREAD_COUNT", v, logic_thread_count_)) return false;
		}
		{
			auto v = ini.sections["SYSTEM"]["CONFIG_FAIL_FAST"];
			if (!v.empty()) {
				int parsed = 0;
				if (dc::cfg::TryParseInt(v, parsed)) {
					config_fail_fast = (parsed != 0);
				}
				else {
					spdlog::warn("[config] invalid SYSTEM.CONFIG_FAIL_FAST='{}' -> default(false)", v);
				}
			}
		}
		{
			auto v = ini.sections["SYSTEM"]["CONFIG_SCHEMA_VERSION"];
			if (!v.empty()) {
				int parsed = config_schema_version;
				if (!parse_int_field("SYSTEM.CONFIG_SCHEMA_VERSION", v, parsed)) return false;
				config_schema_version = parsed;
			}
		}

		// [AOI] (선택)
		// - MAP_W/MAP_H : 임시 맵 크기(월드/존마다 다르면 확장 가능)
		// - WORLD_SIGHT_UNIT : 셀 단위(레거시 WORLD_SIGHT_UNIT)
		// - AOI_RADIUS_CELLS : 주변 셀 반경(1이면 3x3)
		auto g_aoi_ini_cfg = dc::cfg::GetAoiConfig();

		{
			auto v = ini.sections["AOI"]["MAP_W"];
			if (!parse_int_field("AOI.MAP_W", v, g_aoi_ini_cfg.map_size.x)) return false;
		}
		{
			auto v = ini.sections["AOI"]["MAP_H"];
			if (!parse_int_field("AOI.MAP_H", v, g_aoi_ini_cfg.map_size.y)) return false;
		}
		{
			auto v = ini.sections["AOI"]["WORLD_SIGHT_UNIT"];
			if (!parse_int_field("AOI.WORLD_SIGHT_UNIT", v, g_aoi_ini_cfg.world_sight_unit)) return false;
		}
		{
			auto v = ini.sections["AOI"]["AOI_RADIUS_CELLS"];
			if (!parse_int_field("AOI.AOI_RADIUS_CELLS", v, g_aoi_ini_cfg.aoi_radius_cells)) return false;
		}

		// ============================================================
		// ✅ normalize / default rules (최종 확정값 계산)
		// ============================================================

		// 1~3) shard/wait sanity
		dc::cfg::NormalizeShardAndRedisWait(
			db_shard_count_,
			redis_shard_count_,
			redis_wait_replicas_,
			redis_wait_timeout_ms_);

		// 4) flush interval/batch/ttl sanity
		std::string policy_error;
		dc::cfg::WorldRuntimePolicyTargets policy_targets{};
		policy_targets.flush_interval_sec = &flush_interval_sec_;
		policy_targets.char_ttl_sec = &char_ttl_sec_;
		policy_targets.db_pool_size_per_world = &db_pool_size_per_world_;
		policy_targets.flush_batch_immediate = &flush_batch_immediate_;
		policy_targets.flush_batch_normal = &flush_batch_normal_;
		policy_targets.reconnect_grace_close_delay_ms = &reconnect_grace_close_delay_ms_;

		dc::cfg::WorldRuntimePolicyDefaults policy_defaults{};
		policy_defaults.default_flush_interval_sec = kDefaultFlushIntervalSec;
		policy_defaults.default_char_ttl_sec = kDefaultCharTtlSec;
		policy_defaults.default_db_pool_size_per_world = 2;
		policy_defaults.default_batch_immediate = kDefaultBatchImmediate;
		policy_defaults.default_batch_normal = kDefaultBatchNormal;
		policy_defaults.default_reconnect_grace_close_delay_ms = kDefaultReconnectGraceCloseDelayMs;

		const auto policy_table = dc::cfg::BuildWorldRuntimeMinPolicyTable(policy_targets, policy_defaults);
		if (!dc::cfg::ApplyMinPolicies(policy_table.int_specs, policy_table.u32_specs, config_fail_fast, &policy_error)) {
			spdlog::error("{}", policy_error);
			return false;
		}
		if (!dc::cfg::ValidateSchemaCompatibility(
			"SYSTEM.CONFIG_SCHEMA_VERSION",
			config_schema_version,
			dc::cfg::kRuntimeConfigSchemaVersion,
			dc::cfg::kRuntimeConfigSchemaMinSupported,
			dc::cfg::kRuntimeConfigSchemaMaxSupported,
			config_fail_fast,
			&policy_error)) {
			spdlog::error("{}", policy_error);
			return false;
		}
		if (config_schema_version < dc::cfg::kRuntimeConfigSchemaMinSupported ||
			config_schema_version > dc::cfg::kRuntimeConfigSchemaMaxSupported) {
			spdlog::warn(
				"[config] schema version unsupported (continue with auto-heal mode). loaded={} supported=[{},{}]",
				config_schema_version,
				dc::cfg::kRuntimeConfigSchemaMinSupported,
				dc::cfg::kRuntimeConfigSchemaMaxSupported);
		}
		else if (config_schema_version != dc::cfg::kRuntimeConfigSchemaVersion) {
			spdlog::warn(
				"[config] schema version mismatch (continue with auto-heal mode). loaded={} expected={} supported=[{},{}]",
				config_schema_version,
				dc::cfg::kRuntimeConfigSchemaVersion,
				dc::cfg::kRuntimeConfigSchemaMinSupported,
				dc::cfg::kRuntimeConfigSchemaMaxSupported);
		}

		// 5) AOI/섹터 sanity
		dc::cfg::NormalizeAoiConfig(g_aoi_ini_cfg);


		spdlog::info("INI loaded (UTF-8): acc='{}', recv_buf={}",
			db_acc_, world_to_log_recv_buffer_size_);

		spdlog::info("INI(DB_WORK): pool_per_world={}, db_shards={}", db_pool_size_per_world_, db_shard_count_);
		spdlog::info("INI(WRITE_BEHIND): flush_interval={}s, batch_immediate={}, batch_normal={}, ttl={}s",
			flush_interval_sec_, flush_batch_immediate_, flush_batch_normal_, char_ttl_sec_);
		spdlog::info("INI(SESSION): reconnect_grace_close_delay_ms={}", reconnect_grace_close_delay_ms_);
		spdlog::info("INI(SYSTEM): config_fail_fast={} schema_version={} expected_schema_version={} supported_schema_range=[{},{}]",
			config_fail_fast,
			config_schema_version,
			dc::cfg::kRuntimeConfigSchemaVersion,
			dc::cfg::kRuntimeConfigSchemaMinSupported,
			dc::cfg::kRuntimeConfigSchemaMaxSupported);
		spdlog::info("INI(REDIS): shard_count={}, wait_replicas={}, wait_timeout_ms={}",
			redis_shard_count_, redis_wait_replicas_, redis_wait_timeout_ms_);
		spdlog::info("INI(AOI): map={}x{}, unit={}, aoi_r_cells={}",
			g_aoi_ini_cfg.map_size.x, g_aoi_ini_cfg.map_size.y,
			g_aoi_ini_cfg.world_sight_unit, g_aoi_ini_cfg.aoi_radius_cells);

		const auto& w = world_info_;
		spdlog::info("World: name='{}' addr='{}' dsn='{}' db='{}' idx={}",
			w.name_utf8, w.address, w.dsn, w.dbname, w.world_idx);

		return true;
	}

	bool AccountLineRuntime::OnRuntimeInit()
	{
		if (!LoadIniFile()) {
			spdlog::warn("LoadIniFile() failed (stub). Continue with defaults.");
		}
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
			OnLoginCoordinatorRegistered_(sid, serial, server_id, server_name, listen_port);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			OnLoginCoordinatorDisconnected_(sid, serial);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::string_view login_id, std::string_view password) {
			HandleLoginAuthRequest_(sid, serial, trace_id, request_id, login_id, password);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::string_view login_session) {
			HandleWorldListRequest_(sid, serial, trace_id, request_id, account_id, login_session);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint16_t world_id, std::uint16_t channel_id, std::string_view login_session) {
			HandleWorldSelectRequest_(sid, serial, trace_id, request_id, account_id, world_id, channel_id, login_session);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint16_t world_id, std::string_view login_session) {
			HandleCharacterListRequest_(sid, serial, trace_id, request_id, account_id, world_id, login_session);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint64_t char_id, std::string_view login_session) {
			HandleCharacterSelectRequest_(sid, serial, trace_id, request_id, account_id, char_id, login_session);
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
			OnWorldRouteRegistered_(sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags, server_name, public_host, public_port);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			OnWorldRouteDisconnected_(sid, serial);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id,
				std::uint64_t account_id,
				std::string_view login_session, std::string_view world_token) {
			HandleWorldTicketConsumeRequest_(
				sid,
				serial,
				trace_id,
				request_id,
				account_id,
				login_session,
				world_token);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id,
				std::uint64_t account_id, std::uint64_t char_id,
				std::string_view login_session, std::string_view world_token) {
			HandleWorldEnterSuccessNotify_(sid, serial, trace_id, account_id, char_id, login_session, world_token);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id,
				std::uint64_t request_id, std::uint64_t account_id, std::uint16_t world_id, std::uint16_t count, bool ok,
				std::string_view login_session, const pt_aw::WorldCharacterSummary* characters, std::string_view fail_reason) {
			HandleWorldCharacterListResponse_(sid, serial, trace_id, request_id, account_id, world_id, count, ok, login_session, characters, fail_reason);
		},
			[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id,
				std::uint16_t world_id, std::uint16_t channel_id,
				std::uint16_t active_zone_count, std::uint16_t load_score, std::uint32_t flags) {
			OnWorldRouteHeartbeatReceived_(sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags);
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

		{
			std::lock_guard lk(login_character_sessions_mtx_);
			login_character_sessions_.clear();
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









