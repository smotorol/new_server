#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <boost/asio.hpp>
#include <unordered_map>

#include "core/metrics/proc_metrics.h"

#include "net/tcp/tcp_server.h"
#include "net/actor/actor_system.h"

#include "db/core/dqs_types.h"
#include "db/core/dqs_results.h"
#include "db/odbc/odbc_wrapper.h"
#include "db/shard/db_shard_manager.h"

#include "cache/redis/redis_cache.h"

#include "server_common/registry/session_char_registry.h"
#include "server_common/runtime/line_registry.h"
#include "server_common/runtime/line_client_start_helper.h"

#include "services/world/handler/world_handler.h"
#include "services/world/handler/world_control_handler.h"
#include "services/world/actors/world_actors.h"
#include "services/runtime/server_runtime_base.h"
#include "services/world/runtime/i_world_runtime.h"
#include "services/world/runtime/world_line_id.h"
#include "services/world/handler/world_account_handler.h"
#include "services/world/handler/world_zone_handler.h"

#include "proto/client/world_proto.h"
#include "proto/common/packet_util.h"

// World 세팅
struct WorldInfo {
	std::string name_utf8;
	std::string address;
	std::string dsn;
	std::string dbname;
	int port;
	int world_idx = 0;
};

// 월드별 DB Pool(라운드로빈)
struct DbPool
{
	std::vector<db::OdbcConnection> conns;
	std::atomic<std::uint32_t> rr{ 0 };
	db::OdbcConnection& next() { return conns[rr++ % conns.size()]; }
};

namespace svr {

	constexpr std::uint16_t PORT_WORLD = 27787;
	constexpr std::uint16_t PORT_ZONE = 27788;
	constexpr std::uint16_t PORT_CONTROL = 27789;

	class WorldRuntime final : public dc::ServerRuntimeBase, public IWorldRuntime {
	private:
		struct RemoteServiceLineState
		{
			bool registered = false;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint32_t server_id = 0;
			std::string server_name;
			std::uint16_t listen_port = 0;
		};

		struct ZoneRouteState
		{
			bool registered = false;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint32_t server_id = 0;
			std::uint16_t zone_id = 0;
			std::uint16_t world_id = 0;
			std::uint16_t channel_id = 0;
			std::uint16_t map_instance_capacity = 0;
			std::uint16_t active_map_instance_count = 0;
			std::uint16_t active_player_count = 0;
			std::uint16_t load_score = 0;
			std::uint32_t flags = 0;
			std::string server_name;
			std::chrono::steady_clock::time_point last_heartbeat_at{};
		};

		struct DuplicateLoginLogContext
		{
			std::uint64_t trace_id = 0;

			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;

			std::uint32_t sid = 0;
			std::uint32_t serial = 0;

			std::uint32_t new_sid = 0;
			std::uint32_t new_serial = 0;

			std::uint16_t packet_kick_reason = 0;
			DuplicateSessionCause log_cause = DuplicateSessionCause::None;
			SessionKickStatCategory stat_category = SessionKickStatCategory::None;
		};

		struct SessionCloseLogContext
		{
			std::uint64_t trace_id = 0;
			std::uint64_t char_id = 0;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
		};

		struct DelayedCloseKey
		{
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;

			bool operator==(const DelayedCloseKey& rhs) const noexcept
			{
				return sid == rhs.sid && serial == rhs.serial;
			}
		};

		struct DelayedCloseKeyHash
		{
			std::size_t operator()(const DelayedCloseKey& k) const noexcept
			{
				const std::uint64_t packed =
					(static_cast<std::uint64_t>(k.sid) << 32) |
					static_cast<std::uint64_t>(k.serial);
				return std::hash<std::uint64_t>{}(packed);
			}
		};

		struct DelayedCloseEntry
		{
			std::shared_ptr<boost::asio::steady_timer> timer;
			bool armed = false;
			SessionCloseLogContext log_ctx{};
		};

		struct ClosedAuthedSessionContext
		{
			UnbindAuthedWorldSessionResultKind unbind_kind =
				UnbindAuthedWorldSessionResultKind::NotFoundBySid;

			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;

			[[nodiscard]] bool removed() const noexcept
			{
				return unbind_kind == UnbindAuthedWorldSessionResultKind::Removed;
			}
		};

		struct PendingEnterWorldConsumeRequest
		{
			std::uint64_t request_id = 0;
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint64_t account_id = 0;
			std::uint64_t char_id = 0;
			std::string login_session;
			std::string world_token;
			std::chrono::steady_clock::time_point issued_at{};
		};

		struct ZoneRouteInfo
		{
			std::uint32_t sid = 0;
			std::uint32_t serial = 0;
			std::uint32_t zone_server_id = 0;
			std::uint16_t zone_id = 0;
			std::uint16_t active_map_instance_count = 0;
			std::uint16_t active_player_count = 0;
			std::uint16_t load_score = 0;
			std::uint32_t flags = 0;
			std::chrono::steady_clock::time_point last_heartbeat_at{};
		};

		struct MapAssignmentEntry
		{
			std::uint16_t zone_id = 0;
			std::uint32_t map_template_id = 0;
			std::uint32_t instance_id = 0;
		};

		struct PendingZoneAssignRequest
		{
			std::uint64_t request_id = 0;
			std::uint32_t target_sid = 0;
			std::uint32_t target_serial = 0;
			std::uint32_t map_template_id = 0;
			std::uint32_t instance_id = 0;
			std::chrono::steady_clock::time_point issued_at{};
		};

		struct PendingEnterWorldFinalize
		{
			std::uint64_t assign_request_id = 0;
			PendingEnterWorldConsumeRequest enter_pending{};
			std::uint32_t map_template_id = 0;
			std::uint32_t instance_id = 0;
			std::string cached_state_blob;
		};

	public:
		WorldRuntime();
		~WorldRuntime();

		void PostActor(std::uint64_t actor_id, std::function<void()> fn);
		void Post(std::function<void()> fn);

		PlayerActor& GetOrCreatePlayerActor(std::uint64_t char_id);
		WorldActor& GetOrCreateWorldActor();
		void EraseActor(std::uint64_t actor_id);
		ZoneActor& GetOrCreateZoneActor(std::uint32_t zone_id);

		void PostDqsResult(svr::dqs_result::Result r);

		std::uint64_t FindCharIdBySession(std::uint32_t sid) const;

		void CloseWorldServer(std::uint32_t world_socket_index);

		bool PushDQSData(std::uint8_t process_code, std::uint8_t qry_case, const char* data, int size);

		void CacheCharacterState(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob);
		std::optional<std::string> TryLoadCharacterState(std::uint32_t world_code, std::uint64_t char_id);
		void RequestFlushCharacter(std::uint32_t world_code, std::uint64_t char_id);

		void RequestBenchReset() noexcept;
		void RequestBenchMeasure(int seconds) noexcept;

		void OnWorldAuthTicketConsumeResponse(
			std::uint64_t request_id,
			ConsumePendingWorldAuthTicketResultKind result_kind,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token);

		bool RequestConsumeWorldAuthTicket(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view token) override;

		bool NotifyAccountWorldEnterSuccess(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token) override;

		std::uint32_t GetActiveWorldSessionCount() const override;
		std::uint16_t GetActiveZoneCount() const override;

		AssignMapInstanceResult AssignMapInstance(
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			bool create_if_missing,
			bool dungeon_instance) override;

		void OnMapAssignRequest(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::WorldZoneMapAssignRequest& req) override;

		BindAuthedWorldSessionResult BindAuthenticatedWorldSessionForLogin(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint16_t kick_reason) override;

		UnbindAuthedWorldSessionResult UnbindAuthenticatedWorldSessionBySid(
			std::uint32_t sid,
			std::uint32_t serial) override;

		void CancelDelayedWorldClose(
			std::uint32_t sid,
			std::uint32_t serial) override;

		void HandleWorldSessionClosed(
			std::uint32_t sid,
			std::uint32_t serial) override;

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

	private:
		bool LoadIniFile();
		bool DatabaseInit();
		bool NetworkInit();
		bool EnsureAccountHandler_();
		void InitHostedLineDescriptors_() noexcept;

		bool TryReserveDelayedWorldClose_(
			std::uint32_t sid,
			std::uint32_t serial) noexcept;
		bool ArmReservedDelayedWorldClose_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::chrono::milliseconds delay,
			std::uint64_t trace_id,
			std::uint64_t char_id);
		bool UpdateReservedDelayedWorldCloseContext_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t trace_id,
			std::uint64_t char_id) noexcept;
		bool ReleaseDelayedWorldCloseReservation_(
			std::uint32_t sid,
			std::uint32_t serial,
			DelayedCloseEntry* released_entry = nullptr) noexcept;

		static const char* ToString(BindAuthedWorldSessionResultKind kind) noexcept;
		static const char* ToString(UnbindAuthedWorldSessionResultKind kind) noexcept;
		static const char* ToString(DuplicateSessionCause cause) noexcept;
		static const char* ToString(SessionKickStatCategory category) noexcept;

		std::optional<WorldAuthedSession> FindAuthenticatedWorldSessionBySid_(
			std::uint32_t sid) const;

		std::optional<WorldAuthedSession> FindAuthenticatedWorldSessionByCharId_(
			std::uint64_t char_id) const;

		void CancelDelayedWorldCloseTimers_() noexcept;
		void ProcessDuplicateWorldSessionKickOnIo_(
			DuplicateLoginLogContext ctx);

		void ProcessWorldSessionClosedOnIo_(
			std::uint32_t sid,
			std::uint32_t serial);

		void ProcessDuplicateLoginSessionClosedOnIo_(
			const DelayedCloseEntry& released_entry,
			const ClosedAuthedSessionContext& closed_ctx);

		void ProcessNormalSessionClosedOnIo_(
			const ClosedAuthedSessionContext& closed_ctx);
		void CleanupClosedWorldSessionActors_(
			const ClosedAuthedSessionContext& closed_ctx);
		void FailPendingEnterWorldConsumeRequest_(
			const PendingEnterWorldConsumeRequest& pending,
			proto::world::EnterWorldResultCode reason,
			std::string_view log_text);
		void RollbackBoundEnterWorld_(
			const PendingEnterWorldConsumeRequest& pending,
			std::string_view reason_log);
		void FinalizeEnterWorldSuccess_(
			const PendingEnterWorldConsumeRequest& pending,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			std::uint16_t assigned_zone_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
 			std::string_view log_text);

		static ClosedAuthedSessionContext MakeClosedAuthedSessionContext_(
			const UnbindAuthedWorldSessionResult& unbind_result,
			std::uint32_t sid,
			std::uint32_t serial) noexcept;

		static SessionCloseLogContext MakeSessionCloseLogContext_(
			const ClosedAuthedSessionContext& closed_ctx,
			std::uint64_t trace_id = 0,
			std::uint64_t fallback_char_id = 0) noexcept;

		static SessionCloseLogContext ResolveWorldSessionCloseLogContext_(
			const ClosedAuthedSessionContext& closed_ctx,
			const DelayedCloseEntry* released_entry) noexcept;

		void FinalizeWorldSessionClosedOnIo_(
			std::string_view processed_text,
			const ClosedAuthedSessionContext& closed_ctx,
			const SessionCloseLogContext& log_ctx) const;

		void LogWorldSessionClosePostState_(
			const ClosedAuthedSessionContext& closed_ctx,
			std::uint64_t trace_id) const;

		void LogSessionCloseEvent_(
			spdlog::level::level_enum level,
			std::string_view event_text,
			const SessionCloseLogContext& ctx) const;
		void LogSessionCloseProcessed_(
			spdlog::level::level_enum level,
			std::string_view event_text,
			const SessionCloseLogContext& ctx,
			bool removed) const;

		void LogDuplicateWorldSessionEvent_(
			spdlog::level::level_enum level,
			std::string_view event_text,
			const DuplicateLoginLogContext& ctx) const;
		void LogDuplicateWorldSessionCloseDecision_(
			spdlog::level::level_enum level,
			std::string_view decision_text,
			const DuplicateLoginLogContext& ctx) const;

		void EnqueueDuplicateWorldSessionKickClose_(
			DuplicateLoginLogContext ctx);

		void EnqueueDuplicateAuthedSessionCloseIfNeeded_(
			const WorldAuthedSession& victim,
			std::uint64_t fallback_account_id,
			std::uint64_t fallback_char_id,
			std::uint32_t new_sid,
			std::uint32_t new_serial,
			std::uint16_t packet_kick_reason,
			DuplicateSessionCause log_cause,
			SessionKickStatCategory stat_category);

		bool TryBeginDuplicateWorldSessionKickClose_(
			const DuplicateLoginLogContext& ctx);
		template<class HandlerT>
		bool SendDuplicateWorldSessionKick_(
			const DuplicateLoginLogContext& ctx,
			HandlerT& world_handler);
		template<class HandlerT>
		void CloseDuplicateWorldSessionImmediately_(
			const DuplicateLoginLogContext& ctx,
			std::string_view reason,
			HandlerT& world_handler);
		bool CancelDelayedWorldCloseTimer_(
			std::uint32_t sid,
			std::uint32_t serial) noexcept;

		void MarkAccountRegistered_(
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

		void MarkAccountDisconnected_(
			std::uint32_t sid,
			std::uint32_t serial);

		void SendAccountRouteHeartbeat_();
		void ExpireStaleZoneRoutes_(std::chrono::steady_clock::time_point now);
		std::uint64_t MakeMapAssignmentKey_(std::uint32_t map_template_id, std::uint32_t instance_id) const noexcept;
		void RegisterZoneRoute(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneServerHello& req);
		void OnZoneRouteHeartbeat(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneServerRouteHeartbeat& req);
		void UnregisterZoneRoute(std::uint32_t sid, std::uint32_t serial);
		void OnZoneMapAssignResponse(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldMapAssignResponse& res);
		std::optional<ZoneRouteInfo> TrySelectZoneRoute_(bool dungeon_instance) const;
		std::optional<ZoneRouteInfo> FindZoneRouteByZoneId_(std::uint16_t zone_id) const;
		bool SendZonePlayerEnter_(std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id);
		bool SendZonePlayerLeave_(std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id);


		bool InitRedis();
		void ScheduleFlush_();
		void EnqueueFlushDirty_(bool immediate);
		void EnqueueFlushDirtyWorld_(std::uint32_t world_code, std::uint32_t batch);

		bool InitDQS();
		void OnDQSRunOne(std::uint32_t slot_index);
		std::uint32_t RouteShard_(std::uint8_t qry_case, const char* data, int size) const;

		void HandleDqsResult_(const svr::dqs_result::Result& r);

		void BenchResetMain_();
		void BenchStartMain_(int seconds);
		void BenchTickMain_();

	private:
		net::ActorSystem actors_;
		int logic_thread_count_ = 1;

		boost::asio::steady_timer flush_timer_{ io_ };
		boost::asio::strand<boost::asio::io_context::executor_type> duplicate_session_strand_{ io_.get_executor() };

		std::unique_ptr<cache::RedisCache> redis_cache_;

		dc::BasicLineRegistry<svr::WorldLineId, svr::kWorldLineCount> lines_{};

		void RegisterControlLine(
			std::uint32_t sid, std::uint32_t serial,
			std::uint32_t server_id, std::string_view server_name,
			std::uint16_t listen_port);
		void UnregisterControlLine(
			std::uint32_t sid, std::uint32_t serial);

		void RegisterZoneRoute(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t server_id,
			std::uint16_t zone_id,
			std::uint16_t world_id,
			std::uint16_t channel_id,
			std::uint16_t map_instance_capacity,
			std::uint16_t active_map_instance_count,
			std::uint16_t active_player_count,
			std::uint16_t load_score,
			std::uint32_t flags,
			std::string_view server_name);

		void OnZoneRouteHeartbeat(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t server_id,
			std::uint16_t zone_id,
			std::uint16_t world_id,
			std::uint16_t channel_id,
			std::uint16_t map_instance_capacity,
			std::uint16_t active_map_instance_count,
			std::uint16_t active_player_count,
			std::uint16_t load_score,
			std::uint32_t flags);

		mutable std::mutex service_line_mtx_;
		RemoteServiceLineState control_line_state_{};
		std::unordered_map<std::uint32_t, ZoneRouteState> zone_routes_by_sid_{};

		std::atomic<std::uint64_t> next_world_auth_consume_request_id_{ 1 };
		mutable std::mutex pending_enter_world_consume_mtx_;
		std::unordered_map<std::uint64_t, PendingEnterWorldConsumeRequest> pending_enter_world_consume_;
		std::unordered_map<std::uint64_t, MapAssignmentEntry> map_assignments_;
		std::unordered_map<std::uint64_t, PendingZoneAssignRequest> pending_zone_assign_requests_;
		std::unordered_map<std::uint64_t, PendingEnterWorldFinalize> pending_enter_world_finalize_by_assign_request_;
		std::uint64_t next_zone_assign_request_id_ = 1;

		mutable std::mutex world_session_mtx_;

		// 신규 인증 세션 바인딩(account_id + char_id + sid + serial)
		std::unordered_map<std::uint32_t, WorldAuthedSession> authed_sessions_by_sid_;
		std::unordered_map<std::uint64_t, std::uint32_t> authed_sid_by_char_id_;
		std::unordered_map<std::uint64_t, std::uint32_t> authed_sid_by_account_id_;

		std::mutex delayed_world_close_mtx_;
		std::unordered_map<
			DelayedCloseKey,
			DelayedCloseEntry,
			DelayedCloseKeyHash> delayed_world_close_entries_;

		static constexpr std::chrono::milliseconds kDuplicateKickCloseDelay_{ 150 };
		std::atomic<std::uint64_t> duplicate_login_trace_seq_{ 1 };

		static constexpr std::uint32_t MAX_DB_SYC_DATA_NUM = 200000;
		std::vector<svr::dqs::DqsSlot> dqs_slots_;
		std::mutex dqs_mtx_;
		std::deque<std::uint32_t> dqs_empty_;
		std::atomic<std::uint64_t> dqs_drop_count_{ 0 };

		std::uint32_t db_shard_count_ = 1;
		std::unique_ptr<svr::dbshard::DbShardManager> db_shards_;

		std::atomic<bool> bench_req_reset_{ false };
		std::atomic<int> bench_req_measure_seconds_{ 0 };

		bool bench_active_ = false;
		int bench_target_seconds_ = 0;
		int bench_elapsed_seconds_ = 0;
		std::chrono::steady_clock::time_point bench_start_tp_{};
		std::chrono::steady_clock::time_point bench_next_tick_{};

		std::chrono::steady_clock::time_point next_stat_tp_{};
		std::uint64_t last_move_pkts_ = 0;
		std::uint64_t last_move_items_ = 0;

		std::uint64_t bench_base_c2s_move_rx_ = 0;
		std::uint64_t bench_base_s2c_ack_tx_ = 0;
		std::uint64_t bench_base_s2c_ack_drop_ = 0;
		std::uint64_t bench_base_s2c_move_pkts_ = 0;
		std::uint64_t bench_base_s2c_move_items_ = 0;
		std::uint64_t bench_base_send_drops_ = 0;

		std::uint64_t bench_sum_c2s_move_rx_ = 0;
		std::uint64_t bench_sum_s2c_ack_tx_ = 0;
		std::uint64_t bench_sum_s2c_ack_drop_ = 0;
		std::uint64_t bench_sum_s2c_move_pkts_ = 0;
		std::uint64_t bench_sum_s2c_move_items_ = 0;
		std::uint64_t bench_sum_send_drops_ = 0;
		std::size_t bench_max_sendq_msgs_ = 0;
		std::size_t bench_max_sendq_bytes_ = 0;
		std::uint64_t bench_sum_send_drop_bytes_ = 0;
		std::uint64_t bench_sum_app_tx_bytes_ = 0;
		std::uint64_t bench_sum_app_rx_bytes_ = 0;
		double bench_last_cpu_percent_ = 0.0;
		std::uint64_t bench_last_rss_bytes_ = 0;
		procmetrics::ProcSnapshot bench_proc_prev_{};

		std::uint16_t port_world_ = PORT_WORLD;
		std::uint16_t port_zone_ = PORT_ZONE;
		std::uint16_t port_control_ = PORT_CONTROL;

		std::string db_acc_;
		std::string db_pw_;

		std::string redis_host_ = "127.0.0.1";
		int redis_port_ = 6379;
		int redis_db_ = 0;
		std::string redis_password_;
		std::string redis_prefix_ = "dc";

		std::uint32_t redis_shard_count_ = 0;
		int redis_wait_replicas_ = 0;
		int redis_wait_timeout_ms_ = 0;

		int flush_interval_sec_ = 60;
		std::uint32_t flush_batch_immediate_ = 500;
		std::uint32_t flush_batch_normal_ = 200;
		int char_ttl_sec_ = 60 * 60 * 24 * 7;

		int worldset_num_ = 0;
		std::vector<WorldInfo> worlds_;

		std::uint32_t world_to_log_recv_buffer_size_ = 10'000'000;

		std::vector<std::unique_ptr<DbPool>> world_pools_;
		int db_pool_size_per_world_ = 2;

		dc::OutboundLineEntry account_line_{};
		std::shared_ptr<WorldAccountHandler> world_account_handler_;

		std::atomic<bool> account_ready_{ false };
		std::atomic<std::uint32_t> account_sid_{ 0 };
		std::atomic<std::uint32_t> account_serial_{ 0 };
		std::chrono::steady_clock::time_point next_account_route_heartbeat_tp_{};

		std::string account_host_ = "127.0.0.1";
		std::uint16_t account_port_ = 27781;
	};

	extern WorldRuntime g_Main;

	void ServerProgramExit(const char* call_site, bool save);

} // namespace svr
