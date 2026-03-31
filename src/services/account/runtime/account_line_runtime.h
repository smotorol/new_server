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
#include "proto/internal/login_account_proto.h"
#include "services/account/handler/account_world_handler.h"

namespace dc {

	class AccountLineRuntime final : public ServerRuntimeBase {
	public:
		explicit AccountLineRuntime(std::uint16_t login_port, std::uint16_t world_port);
		~AccountLineRuntime() override = default;

	private:
		static constexpr std::uint32_t kMaxDqsSlotCount = 1024;

	public:
		void OnLoginCoordinatorRegisteredFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port);
		void OnLoginCoordinatorDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial);
		void OnLoginAuthRequestFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::string_view login_id, std::string_view password);
		void OnWorldListRequestFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::string_view login_session);
		void OnWorldSelectRequestFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint16_t world_id, std::string_view login_session);
		void OnCharacterListRequestFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint16_t world_id, std::string_view login_session);
		void OnCharacterSelectRequestFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint64_t char_id, std::string_view login_session);
		void OnWorldRouteRegisteredFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id, std::uint16_t world_id, std::uint16_t channel_id, std::uint16_t active_zone_count, std::uint16_t load_score, std::uint32_t flags, std::string_view server_name, std::string_view public_host, std::uint16_t public_port);
		void OnWorldReadyNotifyFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint16_t world_id,std::uint32_t flags);
		void OnWorldRouteDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial);
		void OnWorldRouteHeartbeatReceived(std::uint32_t sid, std::uint32_t serial, std::uint16_t world_id, std::uint16_t active_zone_count, std::uint16_t load_score, std::uint32_t flags);
		void OnWorldTicketConsumeRequestFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::string_view login_session, std::string_view world_token);
		void OnWorldEnterSuccessNotifyFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t account_id, std::uint64_t char_id, std::string_view login_session, std::string_view world_token);
		void OnWorldCharacterListResponseFromHandler(std::uint32_t sid, std::uint32_t serial, std::uint64_t trace_id, std::uint64_t request_id, std::uint64_t account_id, std::uint16_t world_id, std::uint16_t count, bool ok, std::string_view login_session, const pt_aw::WorldCharacterSummary* characters, std::string_view fail_reason);
		void On_world_server_hello(
			std::uint32_t sid,
			std::uint32_t serial,
			std::string_view server_name,
			std::string_view public_host,
			std::uint16_t public_port);
	private:
		struct PendingWorldTicketUpsert
		{
			std::uint64_t trace_id = 0;
			std::uint64_t request_id = 0;
			std::uint32_t login_sid = 0;
			std::uint32_t login_serial = 0;
			std::uint16_t target_world_id = 0;
			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;
			std::string login_session;
			std::string world_token;
			std::string world_host;
			std::uint16_t world_port = 0;
			std::chrono::steady_clock::time_point issued_at{};
		};


		struct ConfiguredWorldEntry
		{
			std::string name;
			std::string db_dns;
			std::string db_id;
			std::string db_pw;
			std::uint16_t world_id = 0;
		};

		struct RegisteredWorldEndpoint
		{
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint16_t world_id = 0;
			std::uint16_t active_zone_count = 0;
			std::uint16_t load_score = 0;
			std::uint32_t flags = 0;
			std::string server_name;
			std::string public_host;
			std::uint16_t public_port = 0;
			bool ready = false;
			std::string db_name;
			std::string db_user;
			std::string db_password;
			std::chrono::steady_clock::time_point registered_at{};
			std::chrono::steady_clock::time_point last_heartbeat_at{};
		};

		struct ConsumedWorldTicketAwaitingNotify
		{
			std::uint64_t trace_id = 0;
			std::uint64_t request_id = 0;
			std::uint32_t login_sid = 0;
			std::uint32_t login_serial = 0;
			std::uint32_t target_world_id = 0;
			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;
			std::string login_session;
			std::string world_token;
			std::string world_host;
			std::uint16_t world_port = 0;
			std::chrono::steady_clock::time_point consumed_at{};
		};

		struct LoginCharacterSession
		{
			std::uint64_t trace_id = 0;
			std::uint32_t login_sid = 0;
			std::uint32_t login_serial = 0;
			std::uint64_t account_id = 0;
			std::uint64_t selected_char_id = 0;
			std::uint16_t selected_world_id = 0;
			std::string selected_world_host;
			std::uint16_t selected_world_port = 0;
			std::string login_session;
			std::vector<proto::internal::login_account::CharacterSummary> cached_characters;
			std::chrono::steady_clock::time_point updated_at{};
		};

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;


		bool BuildWorldSummaryList_(
			std::vector<proto::internal::login_account::WorldSummary>& out_worlds) const;

		bool TryResolveSelectedWorldRoute_(
			std::uint16_t world_id,
			std::string& out_host,
			std::uint16_t& out_port) const;

		bool TrySelectWorldRouteEndpoint_(
			std::string& out_host,
			std::uint16_t& out_port,
			std::uint16_t& out_world_id) const;

		bool TryResolveRegisteredWorldId_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t& out_world_id) const;

		bool TryResolveWorldRouteSessionById_(
			std::uint32_t world_id,
			std::uint32_t& out_sid,
			std::uint32_t& out_serial) const;

		static bool IsWorldSelectable_(const RegisteredWorldEndpoint& endpoint);

		void ErasePendingWorldTicketsForServer_(std::uint32_t world_server_id);

		std::string GenerateWorldToken_() const;

		std::uint16_t ConsumeIssuedWorldTicket_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t trace_id,
			std::uint64_t account_id,
			std::string_view login_session,
			std::string_view world_token,
			ConsumedWorldTicketAwaitingNotify& out_consumed);

		bool TryMatchConsumedWorldEnterSuccessNotify_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t trace_id,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			ConsumedWorldTicketAwaitingNotify& out_consumed);

		void ExpirePendingWorldTickets_(std::chrono::steady_clock::time_point now);
		void ExpireStaleWorldRoutes_(std::chrono::steady_clock::time_point now);
		bool LoadIniFile_();
		
	private:
		bool InitDbWorkers_();
		void ShutdownDbWorkers_();
		bool InitDqs_();
		std::uint32_t RouteAccountShard_(const svr::dqs_payload::AccountAuthRequest& payload) const;
		std::uint32_t RouteAccountShard_(const svr::dqs_payload::AccountCharacterListRequest& payload) const;
		bool PushAccountAuthDqs_(const svr::dqs_payload::AccountAuthRequest& payload);
		bool PushAccountCharacterListDqs_(const svr::dqs_payload::AccountCharacterListRequest& payload);
		void OnDqsRunOne_(std::uint32_t slot_index);
		void RecycleDqsSlot_(std::uint32_t slot_index);
		void PostDqsResult_(svr::dqs_result::Result result);
		void DrainDqsResults_();
		void HandleDqsResult_(const svr::dqs_result::AccountAuthResult& rr);
		void HandleDqsResult_(const svr::dqs_result::AccountCharacterListResult& rr);

	private:
		std::uint16_t login_port_ = 0;
		std::uint16_t world_port_ = 0;

		std::atomic<std::uint32_t> login_sid_{ 0 };
		std::atomic<std::uint32_t> login_serial_{ 0 };
		std::atomic<bool> login_ready_{ false };

		mutable std::mutex world_registry_mtx_;
		std::unordered_map<std::string, ConfiguredWorldEntry> configured_worlds_by_name_;
		std::unordered_map<std::uint32_t, RegisteredWorldEndpoint> worlds_by_sid_;
		std::unordered_map<std::uint32_t, std::uint32_t> world_sid_by_world_id_;

		HostedLineEntry login_line_{};
		HostedLineEntry world_line_{};

		std::shared_ptr<AccountLoginHandler> login_handler_;
		std::shared_ptr<AccountWorldHandler> world_handler_;

		static constexpr auto kWorldRouteHeartbeatTimeout_ = dc::kWorldRouteHeartbeatTimeout_;

		std::mutex pending_world_upsert_mtx_;
		std::unordered_map<std::string, PendingWorldTicketUpsert> pending_world_upserts_;
		std::unordered_map<std::string, ConsumedWorldTicketAwaitingNotify> consumed_world_enters_awaiting_notify_;
		std::mutex login_character_sessions_mtx_;
		std::unordered_map<std::string, LoginCharacterSession> login_character_sessions_;

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
