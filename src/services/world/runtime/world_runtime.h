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
#include "services/world/runtime/world_session_types.h"
#include "services/world/runtime/world_runtime_types.h"
#include "services/world/handler/world_account_handler.h"
#include "services/world/handler/world_zone_handler.h"
#include "server_common/config/server_topology.h"
#include "services/world/common/character_core_state.h"

#include "proto/client/world_proto.h"
#include "proto/common/packet_util.h"

// 월드별 DB Pool(라운드로빈)
struct DbPoolEntry
{
	db::OdbcConnection conn;
	std::mutex mtx;
};

struct DbConnLease
{
	DbPoolEntry* entry = nullptr;
	std::unique_lock<std::mutex> lock{};

	DbConnLease() = default;
	DbConnLease(DbPoolEntry* e, std::unique_lock<std::mutex> lk)
		: entry(e), lock(std::move(lk)) {
	}

	DbConnLease(const DbConnLease&) = delete;
	DbConnLease& operator=(const DbConnLease&) = delete;
	DbConnLease(DbConnLease&&) noexcept = default;
	DbConnLease& operator=(DbConnLease&&) noexcept = default;

	db::OdbcConnection& conn() const { return entry->conn; }
	explicit operator bool() const noexcept { return entry != nullptr; }
};

struct DbPool
{
	std::vector<std::unique_ptr<DbPoolEntry>> conns;
	std::atomic<std::uint32_t> rr{ 0 };

	DbConnLease acquire()
	{
		if (conns.empty()) {
			return {};
		}

		const auto index = rr.fetch_add(1, std::memory_order_relaxed) % static_cast<std::uint32_t>(conns.size());
		auto* entry = conns[index].get();
		return DbConnLease(entry, std::unique_lock<std::mutex>(entry->mtx));
	}

	db::OdbcConnection& front_conn() { return conns.front()->conn; }
};

namespace svr {

	// WorldRuntime 책임 경계
	// - account line: world auth ticket consume / world enter success notify relay
	// - zone line: zone route registry 및 player enter/leave 전달
	// - world runtime: authenticated session / in-world session / actor 소유권의 최종 관리자
	// - account는 식별 검증을, world는 접속 세션 바인딩과 실제 게임 상태 진입을 담당한다.
	constexpr std::uint16_t PORT_WORLD = 27787;
	constexpr std::uint16_t PORT_ZONE = 27788;
	constexpr std::uint16_t PORT_CONTROL = 27789;
	using RemoteServiceLineState = svr::RemoteServiceLineState;
	using ZoneRouteState = svr::ZoneRouteState;
	using DuplicateLoginLogContext = svr::DuplicateLoginLogContext;
	using SessionCloseLogContext = svr::SessionCloseLogContext;
	using DelayedCloseKey = svr::DelayedCloseKey;
	using DelayedCloseKeyHash = svr::DelayedCloseKeyHash;
	using DelayedCloseEntry = svr::DelayedCloseEntry;
	using ClosedAuthedSessionContext = svr::ClosedAuthedSessionContext;
	using PendingEnterWorldConsumeRequest = svr::PendingEnterWorldConsumeRequest;
	using PendingZonePlayerEnterRequest = svr::PendingZonePlayerEnterRequest;
	using ZoneRouteInfo = svr::ZoneRouteInfo;
	using MapAssignmentEntry = svr::MapAssignmentEntry;
	using PendingZoneAssignRequest = svr::PendingZoneAssignRequest;
	using PendingEnterWorldFinalize = svr::PendingEnterWorldFinalize;
	using PendingCharacterEnterSnapshotRequest = svr::PendingCharacterEnterSnapshotRequest;
	using PendingZoneAoiSnapshotRequest = svr::PendingZoneAoiSnapshotRequest;
	using ZoneSnapshotReason = svr::ZoneSnapshotReason;

	class WorldRuntime final : public dc::ServerRuntimeBase {
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

		// account가 검증한 world ticket consume 결과를 받아 world 세션 바인딩을 확정한다.
		void OnWorldAuthTicketConsumeResponse(
			std::uint64_t trace_id,
			std::uint64_t request_id,
			ConsumePendingWorldAuthTicketResultKind result_kind,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token);

		bool RequestConsumeWorldAuthTicket(
			std::uint32_t sid,
			std::uint32_t serial,
			bool use_protobuf,
			std::uint64_t trace_id,
			std::uint64_t account_id,
			std::string_view login_session,
			std::string_view token);

		BeginEnterWorldSessionResult TryBeginEnterWorldSession(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id);

		void CancelPendingEnterWorldSession(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id);

		bool IsEnterWorldSessionPending(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id) const;

		bool PromoteEnterWorldSessionToInWorld(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id);

		void MarkEnterWorldSessionClosing(
			std::uint32_t sid,
			std::uint32_t serial);

		// world가 최종 enter success를 account에 relay 한다. login pending session 정리는 account/login이 담당한다.
		bool NotifyAccountWorldEnterSuccess(
			std::uint64_t trace_id,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token);

		void OnAccountCharacterListRequest(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t trace_id,
			std::uint64_t request_id,
			std::uint64_t account_id,
			std::uint16_t world_id,
			std::string_view login_session);

		// handler -> runtime bridge
		void OnAccountRegisterAckFromHandler(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint16_t world_id,
			std::string_view db_dns,
			std::string_view db_id,
			std::string_view db_pw);

		void OnAccountDisconnectedFromHandler(
			std::uint32_t sid,
			std::uint32_t serial);

		void OnZoneRegisteredFromHandler(
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

		void OnZoneHeartbeatFromHandler(
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

		void OnZoneDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial);
		void OnZoneMapAssignResponseFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldMapAssignResponse& res);
		void OnZonePlayerEnterAckFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldPlayerEnterAck& ack);
		void OnZoneAoiSnapshotFromHandler(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::ZoneWorldAoiSnapshot& snapshot,
			const proto::S2C_player_spawn_item* items,
			std::size_t count);
		void OnZoneAoiSpawnBatchFromHandler(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::ZoneWorldAoiSpawnBatch& batch,
			const proto::S2C_player_spawn_item* items,
			std::size_t count);
		void OnZoneAoiDespawnBatchFromHandler(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::ZoneWorldAoiDespawnBatch& batch,
			const proto::S2C_player_despawn_item* items,
			std::size_t count);
		void OnZoneAoiMoveBatchFromHandler(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::ZoneWorldAoiMoveBatch& batch,
			const proto::S2C_player_move_item* items,
			std::size_t count);
		bool RequestZoneInitialSnapshot(
			std::uint64_t trace_id,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id,
			std::uint16_t zone_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			std::int32_t x,
			std::int32_t y,
			ZoneSnapshotReason reason);
		bool MirrorPlayerMoveToZone(
			std::uint64_t trace_id,
			std::uint64_t request_id,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id,
			std::uint16_t zone_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			std::int32_t x,
			std::int32_t y);
		bool IsZoneAoiMoveDiffEnabled() const noexcept { return enable_zone_aoi_move_diff_; }
		bool IsWorldAoiMoveDiffFallbackEnabled() const noexcept { return false; }

		void OnControlRegisteredFromHandler(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint32_t server_id,
			std::string_view server_name,
			std::uint16_t listen_port);

		void OnControlDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial);

		std::string_view GetWorldName() { return name_utf8_; };
		std::uint32_t GetActiveWorldSessionCount() const;
		std::uint16_t GetActiveZoneCount() const;
		std::string_view GetGateIP() const { return host_; };
		std::uint16_t GetGatePort() const { return port_world_; };
		std::uint16_t GetWorldID() const { return world_id_; };

		AssignMapInstanceResult AssignMapInstance(
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			bool create_if_missing,
			bool dungeon_instance,
			std::uint64_t trace_id = 0);


		std::optional<MapAssignmentEntry> TryGetMapAssignment(
			std::uint32_t map_template_id,
			std::uint32_t instance_id) const;
		bool BeginPortalTransfer(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id,
			std::uint16_t source_zone_id,
			std::uint32_t source_map_template_id,
			std::uint32_t source_instance_id,
			std::int32_t source_x,
			std::int32_t source_y,
			std::uint32_t target_map_template_id,
			std::uint32_t target_instance_id,
			std::int32_t target_x,
			std::int32_t target_y,
			std::uint64_t trace_id = 0);

		void OnMapAssignRequest(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::WorldZoneMapAssignRequest& req);

		BindAuthedWorldSessionResult BindAuthenticatedWorldSessionForLogin(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint16_t kick_reason);

		UnbindAuthedWorldSessionResult UnbindAuthenticatedWorldSessionBySid(
			std::uint32_t sid,
			std::uint32_t serial);

		void CancelDelayedWorldClose(
			std::uint32_t sid,
			std::uint32_t serial);

		void MarkWorldSessionCloseReason(
			std::uint32_t sid,
			std::uint32_t serial,
			WorldSessionCloseReason reason);

		void HandleWorldSessionClosed(
			std::uint32_t sid,
			std::uint32_t serial);

		std::optional<WorldAuthedSession> TryGetAuthenticatedWorldSession(
			std::uint32_t sid,
			std::uint32_t serial) const;
		std::string GetOrCreateReconnectTokenForSession(
			std::uint32_t sid,
			std::uint32_t serial);
		ReconnectWorldSessionResult TryReconnectWorldSession(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view reconnect_token,
			std::uint32_t sid,
			std::uint32_t serial);

	private:
		bool OnRuntimeInit() override;
		void OnBeforeIoStop() override;
		void OnAfterIoStop() override;
		void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

	private:
		bool LoadIniFile();
		bool DatabaseInit();
		bool NetworkInit();
		bool PreloadItemTemplateRepository_();
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
		void BeginLeaveWorld_(
			const LeaveWorldContext& ctx,
			std::string_view reason_log);
		void FailPendingEnterWorldConsumeRequest_(
			const PendingEnterWorldConsumeRequest& pending,
			proto::world::EnterWorldResultCode reason,
			std::string_view log_text);
		void RollbackBoundEnterWorld_(
			const PendingEnterWorldConsumeRequest& pending,
			std::string_view reason_log,
			const LeaveWorldContext* leave_ctx = nullptr);
		void FinalizeEnterWorldSuccess_(
			const PendingEnterWorldConsumeRequest& pending,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token,
			std::uint16_t assigned_zone_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			const CharacterCoreState& core_state);
		void CompleteEnterWorldSuccessAfterZoneAck_(
			const PendingZonePlayerEnterRequest& pending_enter);
		bool RequestCharacterEnterSnapshotLoad_(
			const PendingEnterWorldConsumeRequest& pending);
		bool RequestAccountCharacterListLoad_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t trace_id,
			std::uint64_t request_id,
			std::uint64_t account_id,
			std::uint16_t world_id,
			std::string_view login_session);
		void OnCharacterEnterSnapshotResult_(
			const svr::dqs_result::WorldCharacterEnterSnapshotResult& rr);
		void OnAccountCharacterListResult_(
			const svr::dqs_result::WorldAccountCharacterListResult& rr);
		static void ApplyCharacterCoreStateToActor_(
			PlayerActor& actor,
			const CharacterCoreState& core_state,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint16_t assigned_zone_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id);

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

		bool ApplyAccountAssignedConfig_(std::uint16_t world_id, std::string_view db_dns, std::string_view db_id, std::string_view db_pw);
		void NotifyAccountReady_();

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
		void OnZonePlayerEnterAck(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldPlayerEnterAck& ack);
		std::optional<ZoneRouteInfo> TrySelectZoneRoute_(bool dungeon_instance) const;
		std::optional<ZoneRouteInfo> TrySelectZoneRouteForMap_(std::uint32_t map_template_id, bool dungeon_instance) const;
		std::optional<ZoneRouteInfo> FindZoneRouteByZoneId_(std::uint16_t zone_id) const;
		std::optional<ZoneRouteInfo> FindZoneRouteByServerChannel_(std::uint32_t zone_server_id, std::uint16_t channel_id) const;
		std::uint32_t CountPendingZoneAssignRequestsBySid_(std::uint32_t sid, std::uint32_t serial) const;
		std::uint32_t CountPendingZonePlayerEnterRequestsBySid_(std::uint32_t sid, std::uint32_t serial) const;
		std::uint32_t ComputeEffectiveRoutePressure_(const ZoneRouteInfo& route) const;
		std::uint32_t ResolvePortalTargetInstanceId_(std::uint32_t map_template_id, std::uint32_t requested_instance_id);
		static bool IsDungeonMapTemplate_(std::uint32_t map_template_id) noexcept;
		void CompletePortalTransfer_(
			const PendingPortalTransfer& pending,
			std::uint16_t assigned_zone_id,
			std::uint16_t assigned_channel_id,
			std::uint32_t assigned_zone_server_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id);
		void FailPendingPortalTransfer_(
			const PendingPortalTransfer& pending,
			std::string_view reason);
		void ExpirePendingZoneAoiSnapshots_(std::chrono::steady_clock::time_point now);
		void SendWorldFallbackZoneSnapshot_(const PendingZoneAoiSnapshotRequest& pending);
		std::optional<ZoneRouteInfo> ResolveZoneRouteForMapInstance_(
			std::uint16_t zone_id,
			std::uint32_t map_template_id,
			std::uint32_t instance_id) const;
		bool SendZonePlayerEnter_(std::uint64_t trace_id, std::uint16_t zone_id, std::uint64_t request_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id);
		bool SendZonePlayerLeave_(std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id);
		void LoadServerTopology_();
		void RemoveMapAssignmentsByZoneSid_(std::uint32_t sid);
		void FailPendingZoneAssignRequestsByZoneSid_(
			std::uint32_t sid,
			proto::world::EnterWorldResultCode reason,
			std::string_view log_text,
			std::string_view rollback_log);
		static proto::world::EnterWorldResultCode MapAssignFailureToEnterWorldReason_(
			AssignMapInstanceResultKind kind,
			std::uint16_t reject_result_code = 0) noexcept;
		static proto::world::EnterWorldResultCode MapZoneAssignRejectCodeToEnterWorldReason_(
			std::uint16_t zone_result_code) noexcept;
		static proto::world::EnterWorldResultCode MapZonePlayerEnterRejectCodeToEnterWorldReason_(
			std::uint16_t zone_result_code) noexcept;
		void FailPendingZonePlayerEnterRequestsByZoneSid_(
			std::uint32_t sid,
			proto::world::EnterWorldResultCode reason,
			std::string_view log_text,
			std::string_view rollback_log);
		void ErasePendingEnterWorldConsumeRequestsBySession_(
			std::uint32_t sid,
			std::uint32_t serial);
		void ErasePendingEnterWorldFinalizeBySession_(
			std::uint32_t sid,
			std::uint32_t serial);
		void ErasePendingCharacterEnterSnapshotRequestsBySession_(
			std::uint32_t sid,
			std::uint32_t serial);
		void ErasePendingZonePlayerEnterRequestsBySession_(
			std::uint32_t sid,
			std::uint32_t serial);
		void AbortEnterWorldFlowBySession_(
			std::uint32_t sid,
			std::uint32_t serial,
			std::string_view log_text);

		bool InitRedis();
		void ScheduleFlush_();
		void EnqueueFlushDirty_(bool immediate);
		void EnqueueFlushDirtyWorld_(std::uint32_t world_code, std::uint32_t batch);
		std::size_t CountInFlightDqs_() const;
		void UpdateExpectedCharVersion_(std::uint32_t world_code, std::uint64_t char_id, std::uint32_t version);
		std::uint32_t TryGetExpectedCharVersion_(std::uint32_t world_code, std::uint64_t char_id) const;
		void EraseExpectedCharVersion_(std::uint32_t world_code, std::uint64_t char_id);

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
		std::optional<dc::cfg::ServerTopology> server_topology_{};

		std::atomic<std::uint64_t> next_world_auth_consume_request_id_{ 1 };
		mutable std::mutex pending_enter_world_consume_mtx_;
		std::unordered_map<std::uint64_t, PendingEnterWorldConsumeRequest> pending_enter_world_consume_;
		std::unordered_map<std::uint64_t, MapAssignmentEntry> map_assignments_;
		std::unordered_map<std::uint64_t, PendingZoneAssignRequest> pending_zone_assign_requests_;
		std::unordered_map<std::uint32_t, std::uint32_t> next_dynamic_instance_id_by_map_;
		std::unordered_map<std::uint64_t, std::uint64_t> pending_zone_assign_request_id_by_map_key_;
		std::unordered_map<std::uint64_t, PendingCharacterEnterSnapshotRequest> pending_character_enter_snapshot_requests_;
		std::unordered_map<std::uint64_t, std::vector<PendingEnterWorldFinalize>> pending_enter_world_finalize_by_assign_request_;
		std::unordered_map<std::uint64_t, PendingPortalTransfer> pending_portal_transfer_by_assign_request_;
		std::unordered_map<std::uint64_t, PendingZonePlayerEnterRequest> pending_zone_player_enter_requests_;
		std::unordered_map<std::uint64_t, PendingZoneAoiSnapshotRequest> pending_zone_aoi_snapshot_by_session_key_;
		mutable std::mutex pending_zone_aoi_snapshot_mtx_;
		std::uint64_t next_zone_assign_request_id_ = 1;
		std::uint64_t next_zone_player_enter_request_id_ = 1;

		mutable std::mutex world_session_mtx_;

		// 신규 인증 세션 바인딩(account_id + char_id + sid + serial)
		std::unordered_map<std::uint32_t, WorldAuthedSession> authed_sessions_by_sid_;
		std::unordered_map<std::uint64_t, std::uint64_t> authed_session_key_by_char_id_;
		std::unordered_map<std::uint64_t, std::uint64_t> authed_session_key_by_account_id_;
		std::unordered_map<std::string, std::uint64_t> reconnect_session_key_by_token_;
		std::unordered_map<std::uint64_t, WorldSessionCloseReason> session_close_reason_by_session_key_;
		std::unordered_map<std::uint64_t, WorldEnterStage> world_enter_stage_by_session_key_;
		std::unordered_map<std::uint64_t, std::uint64_t> pending_enter_session_key_by_char_id_;


		std::mutex delayed_world_close_mtx_;
		std::unordered_map<
			DelayedCloseKey,
			DelayedCloseEntry,
			DelayedCloseKeyHash> delayed_world_close_entries_;

		static constexpr std::chrono::milliseconds kDuplicateKickCloseDelay_{ 150 };
		static constexpr std::chrono::milliseconds kZoneSnapshotFallbackDelay_{ 250 };
		std::atomic<std::uint64_t> duplicate_login_trace_seq_{ 1 };
		mutable std::mutex expected_char_ver_mtx_;
		std::unordered_map<std::uint64_t, std::uint32_t> expected_char_version_by_key_;

		static constexpr std::uint32_t MAX_DB_SYC_DATA_NUM = 200000;
		std::vector<svr::dqs::DqsSlot> dqs_slots_;
		mutable std::mutex dqs_mtx_;
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
		std::uint64_t last_aoi_entered_entities_ = 0;
		std::uint64_t last_aoi_exited_entities_ = 0;
		std::uint64_t last_aoi_move_fanout_ = 0;
		std::uint64_t last_aoi_move_events_ = 0;
		std::uint64_t last_aoi_sanitize_removed_entered_ = 0;
		std::uint64_t last_aoi_sanitize_removed_exited_ = 0;
		std::uint64_t last_aoi_sanitize_removed_new_vis_ = 0;
		std::uint64_t last_unauth_packet_rejects_ = 0;
		std::uint64_t last_dup_login_char_ = 0;
		std::uint64_t last_dup_login_account_ = 0;
		std::uint64_t last_dup_login_both_ = 0;
		std::uint64_t last_dup_login_dedup_same_session_ = 0;
		std::uint64_t last_flush_dirty_conflicts_total_ = 0;
		std::uint64_t last_flush_dirty_conflicted_batches_ = 0;
		bool enable_zone_aoi_snapshot_ = true;
		bool enable_zone_aoi_move_diff_ = true;
		bool enable_world_aoi_move_diff_fallback_ = false;

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
		int reconnect_grace_close_delay_ms_ = 5000;
		bool allow_legacy_item_template_fallback_ = true;

		std::uint32_t world_to_log_recv_buffer_size_ = 10'000'000;

		std::unique_ptr<DbPool> world_pool_;
		int db_pool_size_per_world_ = 2;

		dc::OutboundLineEntry account_line_{};
		std::shared_ptr<WorldAccountHandler> world_account_handler_;

		std::atomic<bool> account_ready_{ false };
		std::atomic<std::uint32_t> account_sid_{ 0 };
		std::atomic<std::uint32_t> account_serial_{ 0 };
		std::chrono::steady_clock::time_point next_account_route_heartbeat_tp_{};

		std::string name_utf8_ = "world";
		std::string host_ = "127.0.0.1";
		std::string account_host_ = "127.0.0.1";
		std::uint16_t account_port_ = 27781;
		std::uint16_t port_world_ = PORT_WORLD;
		std::uint16_t port_zone_ = PORT_ZONE;
		std::uint16_t port_control_ = PORT_CONTROL;

		std::string db_dns_;
		std::string db_id_;
		std::string db_pw_;

		uint16_t world_id_ = 0;
	};

	extern WorldRuntime g_Main;

	void ServerProgramExit(const char* call_site, bool save);

} // namespace svr





