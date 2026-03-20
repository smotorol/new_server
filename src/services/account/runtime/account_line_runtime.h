#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "server_common/runtime/line_registry.h"
#include "services/account/handler/account_login_handler.h"
#include "services/runtime/server_runtime_base.h"

#include "db/core/dqs_payloads.h"
#include "db/core/dqs_results.h"
#include "db/core/dqs_types.h"
#include "db/odbc/odbc_wrapper.h"
#include "db/shard/db_shard_manager.h"
#include "services/account/handler/account_world_handler.h"

namespace dc {

	// AccountLineRuntime responsibilities
	// - Login line: authenticate login requests and issue world-enter tickets.
	// - World line: maintain selectable world routes and consume issued tickets.
	// - Account runtime brokers auth/ticket/notify only; world session ownership is finalized by WorldRuntime.
	class AccountLineRuntime final : public ServerRuntimeBase {
	public:
		explicit AccountLineRuntime(std::uint16_t login_port, std::uint16_t world_port);
		~AccountLineRuntime() override = default;

	private:
		static constexpr std::uint32_t kMaxDqsSlotCount = 1024;

	private:
		struct PendingWorldTicketUpsert
		{
			std::uint64_t request_id = 0;
			std::uint32_t login_sid = 0;
			std::uint32_t login_serial = 0;
			std::uint32_t target_world_server_id = 0;
			std::uint16_t target_world_id = 0;
			std::uint16_t target_channel_id = 0;

			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;

			std::string login_session;
			std::string world_token;
			std::string world_host;
			std::uint16_t world_port = 0;

			std::chrono::steady_clock::time_point issued_at{};
		};

		struct RegisteredWorldEndpoint
		{
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint32_t server_id = 0;
			std::uint16_t world_id = 0;
			std::uint16_t channel_id = 0;
			std::uint16_t active_zone_count = 0;
			std::uint16_t load_score = 0;
			std::uint32_t flags = 0;
			std::string server_name;
			std::string public_host;
			std::uint16_t public_port = 0;
			std::chrono::steady_clock::time_point registered_at{};
			std::chrono::steady_clock::time_point last_heartbeat_at{};
		};

		struct ConsumedWorldTicketAwaitingNotify
		{
			std::uint64_t request_id = 0;
			std::uint32_t login_sid = 0;
			std::uint32_t login_serial = 0;
			std::uint32_t target_world_server_id = 0;

			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;

			std::string login_session;
			std::string world_token;
			std::string world_host;
			std::uint16_t world_port = 0;

			std::chrono::steady_clock::time_point consumed_at{};
		};

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

		void OnLoginCoordinatorRegistered_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t server_id,
			std::string_view server_name,
			std::uint16_t listen_port);

		void OnLoginCoordinatorDisconnected_(
			std::uint32_t sid,
			std::uint32_t serial);

		void HandleLoginAuthRequest_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t request_id,
			std::string_view login_id,
			std::string_view password,
			std::uint64_t selected_char_id);

		void OnWorldRouteRegistered_(
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
			std::uint16_t public_port);

		void OnWorldRouteDisconnected_(
			std::uint32_t sid,
			std::uint32_t serial);

		void OnWorldRouteHeartbeatReceived_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t server_id,
			std::uint16_t world_id,
			std::uint16_t channel_id,
			std::uint16_t active_zone_count,
			std::uint16_t load_score,
			std::uint32_t flags);

		bool TrySelectWorldRouteEndpoint_(
			std::string& out_host,
			std::uint16_t& out_port,
			std::uint32_t& out_server_id,
			std::uint16_t& out_world_id,
			std::uint16_t& out_channel_id) const;

		bool TryResolveRegisteredWorldServerId_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t& out_server_id) const;

		static bool IsWorldSelectable_(const RegisteredWorldEndpoint& endpoint);

		void ErasePendingWorldTicketsForServer_(std::uint32_t world_server_id);

		std::string GenerateWorldToken_() const;

		void HandleWorldTicketConsumeRequest_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t request_id,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token);

		std::uint16_t ConsumeIssuedWorldTicket_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			ConsumedWorldTicketAwaitingNotify& out_consumed);

		void HandleWorldEnterSuccessNotify_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token);

		bool TryMatchConsumedWorldEnterSuccessNotify_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			ConsumedWorldTicketAwaitingNotify& out_consumed);

		void ExpirePendingWorldTickets_(std::chrono::steady_clock::time_point now);
		void ExpireStaleWorldRoutes_(std::chrono::steady_clock::time_point now);
	private:
		bool InitDbWorkers_();
		void ShutdownDbWorkers_();

		bool InitDqs_();
		std::uint32_t RouteAccountShard_(const svr::dqs_payload::AccountAuthRequest& payload) const;
		bool PushAccountAuthDqs_(const svr::dqs_payload::AccountAuthRequest& payload);

		void OnDqsRunOne_(std::uint32_t slot_index);
		void RecycleDqsSlot_(std::uint32_t slot_index);

		void PostDqsResult_(svr::dqs_result::Result result);
		void DrainDqsResults_();
		void HandleDqsResult_(const svr::dqs_result::AccountAuthResult& rr);

	private:
		std::uint16_t login_port_ = 0;
		std::uint16_t world_port_ = 0;

		std::atomic<std::uint32_t> login_sid_{ 0 };
		std::atomic<std::uint32_t> login_serial_{ 0 };
		std::atomic<bool> login_ready_{ false };

		mutable std::mutex world_registry_mtx_;
		std::unordered_map<std::uint32_t, RegisteredWorldEndpoint> worlds_by_sid_;
		std::unordered_map<std::uint32_t, std::uint32_t> world_sid_by_server_id_;

		HostedLineEntry login_line_{};
		HostedLineEntry world_line_{};

		std::shared_ptr<AccountLoginHandler> login_handler_;
		std::shared_ptr<AccountWorldHandler> world_handler_;

		static constexpr auto kWorldRouteHeartbeatTimeout_ = dc::kWorldRouteHeartbeatTimeout_;

		std::mutex pending_world_upsert_mtx_;
		std::unordered_map<std::string, PendingWorldTicketUpsert> pending_world_upserts_;
		std::unordered_map<std::string, ConsumedWorldTicketAwaitingNotify> consumed_world_enters_awaiting_notify_;

	private:
		std::string db_conn_str_ =
			"DRIVER={ODBC Driver 18 for SQL Server};"
			"SERVER=127.0.0.1,11433;"
			"UID=sa;"
			"PWD=Strong!Pass123;"
			"Encrypt=optional;"
			"DATABASE=NFX_AUTH;";

		std::uint32_t db_shard_count_ = 2;
		std::vector<std::unique_ptr<db::OdbcConnection>> db_worker_conns_;
		std::unique_ptr<svr::dbshard::DbShardManager> db_shards_;

	private:
		std::mutex dqs_mtx_;
		std::vector<svr::dqs::DqsSlot> dqs_slots_;
		std::deque<std::uint32_t> dqs_empty_;
		std::atomic<std::uint64_t> dqs_push_count_{ 0 };
		std::atomic<std::uint64_t> dqs_drop_count_{ 0 };

		std::mutex dqs_result_mtx_;
		std::deque<svr::dqs_result::Result> dqs_results_;
	};

} // namespace dc
