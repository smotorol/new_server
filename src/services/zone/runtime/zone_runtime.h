#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "server_common/runtime/line_client_start_helper.h"
#include "services/runtime/server_runtime_base.h"

#include "proto/internal/world_zone_proto.h"

namespace pt_wz = proto::internal::world_zone;

class ZoneWorldHandler;

namespace svr {

class ZoneRuntime final : public dc::ServerRuntimeBase
{
private:
    struct MapInstanceState
    {
        std::uint32_t map_template_id = 0;
        std::uint32_t instance_id = 0;
        bool dungeon_instance = false;
        std::uint16_t active_player_count = 0;
        std::chrono::steady_clock::time_point created_at{};
        std::chrono::steady_clock::time_point last_access_at{};
    };

    struct PlayerBindingState
    {
        std::uint64_t map_key = 0;
        std::uint32_t map_template_id = 0;
        std::uint32_t instance_id = 0;
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t cell_x = 0;
        std::int32_t cell_y = 0;
        bool has_position = false;
    };

    struct SpatialIndexState
    {
        std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> cells{};
    };

    struct AoiPerfStats
    {
        std::uint64_t move_count = 0;
        std::uint64_t visible_calc_total_us = 0;
        std::uint64_t visible_calc_max_us = 0;
        std::uint64_t diff_calc_total_us = 0;
        std::uint64_t diff_calc_max_us = 0;
        std::uint64_t batch_build_total_us = 0;
        std::uint64_t batch_build_max_us = 0;
        std::uint64_t total_move_total_us = 0;
        std::uint64_t total_move_max_us = 0;
        std::uint64_t candidate_total = 0;
        std::uint64_t candidate_max = 0;
        std::uint64_t visible_after_total = 0;
        std::uint64_t visible_after_max = 0;
        std::deque<std::uint32_t> recent_total_move_us{};
    };

public:
    ZoneRuntime();
    ~ZoneRuntime();

    std::uint16_t GetActiveMapInstanceCount() const noexcept;
    std::uint16_t GetMapInstanceCapacity() const noexcept;
    std::uint16_t GetLoadScore() const noexcept;
    std::uint16_t GetActivePlayerCount() const noexcept;
    std::uint32_t GetFlagsFromHandler() const noexcept { return flags_; }
    void OnWorldRegisterAckFromHandler(std::uint32_t sid, std::uint32_t serial);
    void OnWorldDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial);
    void OnMapAssignRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZoneMapAssignRequest& req);
    void OnPlayerEnterRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerEnter& req);
    void OnPlayerLeaveRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerLeave& req);
    void OnPlayerMoveInternalRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerMoveInternal& req);

private:
    bool OnRuntimeInit() override;
    void OnBeforeIoStop() override;
    void OnAfterIoStop() override;
    void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

    bool LoadIniFile_();
    bool NetworkInit_();
    bool OwnsMapTemplate_(std::uint32_t map_template_id) const noexcept;
    std::uint16_t ResolveGameplayZoneId_(std::uint32_t map_template_id) const noexcept;
    void MarkWorldRegistered_(std::uint32_t sid, std::uint32_t serial);
    void MarkWorldDisconnected_(std::uint32_t sid, std::uint32_t serial);
    void SendWorldHeartbeat_();
    void OnMapAssignRequest(
        std::uint32_t sid,
        std::uint32_t serial,
        const pt_wz::WorldZoneMapAssignRequest& req);
    void EnsureMapInstance_(
        std::uint32_t map_template_id,
        std::uint32_t instance_id,
        bool create_if_missing,
        bool dungeon_instance);
    void OnPlayerEnterRequest_(
        std::uint32_t sid,
        std::uint32_t serial,
        const pt_wz::WorldZonePlayerEnter& req);
    void OnPlayerLeaveRequest_(
        std::uint32_t sid,
        std::uint32_t serial,
        const pt_wz::WorldZonePlayerLeave& req);
    void OnPlayerMoveInternalRequest_(
        std::uint32_t sid,
        std::uint32_t serial,
        const pt_wz::WorldZonePlayerMoveInternal& req);
    void ReapEmptyDungeonInstances_(std::chrono::steady_clock::time_point now);
    void RefreshMetrics_();
    static std::int32_t ResolveAoiCellSize_() noexcept;
    static std::uint64_t MakeSpatialCellKey_(std::int32_t cell_x, std::int32_t cell_y) noexcept;
    static std::int32_t ResolveSpatialCellCoord_(std::int32_t value, std::int32_t cell_size) noexcept;
    void InsertActorIntoSpatialIndex_(std::uint64_t map_key, std::uint64_t char_id, std::int32_t cell_x, std::int32_t cell_y);
    void RemoveActorFromSpatialIndex_(std::uint64_t map_key, std::uint64_t char_id, std::int32_t cell_x, std::int32_t cell_y);
    void UpdateActorSpatialIndex_(std::uint64_t map_key, std::uint64_t char_id, const PlayerBindingState& before_state, const PlayerBindingState& after_state);
    void RecordAoiPerf_(
        std::uint64_t map_key,
        std::uint64_t char_id,
        std::size_t candidate_count,
        std::size_t visible_before_count,
        std::size_t visible_after_count,
        std::size_t occupied_cells,
        std::size_t neighbor_cell_count,
        std::uint64_t visible_calc_time_us,
        std::uint64_t diff_calc_time_us,
        std::uint64_t spawn_build_time_us,
        std::uint64_t despawn_build_time_us,
        std::uint64_t move_build_time_us,
        std::uint64_t total_move_time_us);
    bool EnsureAoiLogFilesReady_();
    bool EnsureAoiLogFileReady_(
        std::ofstream& stream,
        const std::filesystem::path& path,
        const char* header);
    void RotateAoiLogFileIfNeeded_(
        std::ofstream& stream,
        const std::filesystem::path& path,
        const char* header);
    static std::uint64_t CurrentUnixTimestampMs_();
    static std::int32_t DecodeSpatialCellX_(std::uint64_t cell_key) noexcept;
    static std::int32_t DecodeSpatialCellY_(std::uint64_t cell_key) noexcept;
    void EmitAoiPerfSummaryLogs_(std::chrono::steady_clock::time_point now);
    void EmitAoiHotspotLogs_(std::chrono::steady_clock::time_point now);
    std::vector<std::uint64_t> BuildVisibleActorIdsForPosition_(
        std::uint64_t char_id,
        std::uint64_t map_key,
        std::int32_t x,
        std::int32_t y,
        std::size_t* out_candidate_count = nullptr,
        std::size_t* out_neighbor_cell_count = nullptr) const;
    std::vector<proto::S2C_player_spawn_item> BuildVisibleSnapshotItems_(
        std::uint64_t char_id,
        const PlayerBindingState& self_state) const;
    std::vector<proto::S2C_player_spawn_item> BuildSpawnItemsForActorIds_(
        const std::vector<std::uint64_t>& actor_ids) const;
    bool TrySendInitialAoiSnapshot_(
        std::uint64_t trace_id,
        std::uint64_t char_id,
        const PlayerBindingState& self_state,
        std::uint64_t snapshot_request_id);
    bool ValidateZoneMapRuntimeData_(std::uint32_t map_template_id) const;
    void LogZoneRegionSummary_(std::uint32_t map_template_id, std::int32_t x, std::int32_t y) const;
    static std::uint64_t MakeMapInstanceKey_(std::uint32_t map_template_id, std::uint32_t instance_id) noexcept;
    static bool IsDungeonMapTemplate_(std::uint32_t map_template_id) noexcept;

private:
    std::uint32_t zone_server_id_ = 2001;
    std::uint16_t zone_id_ = 1;
    std::uint16_t world_id_ = 1;
    std::uint16_t channel_id_ = 1;
    std::string server_name_ = "zone-1";

    std::string world_host_ = "127.0.0.1";
    std::uint16_t world_port_ = 27788;

    std::uint16_t map_instance_capacity_ = 128;
    std::vector<std::uint32_t> served_map_ids_{ 1 };
    std::unordered_map<std::uint64_t, MapInstanceState> map_instances_{};
    std::unordered_map<std::uint64_t, PlayerBindingState> player_bindings_{};
    std::unordered_map<std::uint64_t, SpatialIndexState> spatial_index_by_map_key_{};
    std::atomic<std::uint16_t> active_map_instance_count_{ 0 };
    std::atomic<std::uint16_t> active_player_count_{ 0 };
    std::atomic<std::uint16_t> load_score_{ 0 };
    std::uint32_t flags_ = 1;

    dc::OutboundLineEntry world_line_{};
    std::shared_ptr<ZoneWorldHandler> zone_world_handler_;
    std::atomic<bool> world_ready_{ false };
    std::atomic<std::uint32_t> world_sid_{ 0 };
    std::atomic<std::uint32_t> world_serial_{ 0 };
    std::chrono::steady_clock::time_point next_world_heartbeat_tp_{};
    std::chrono::steady_clock::time_point next_reap_tp_{};
    std::chrono::steady_clock::time_point next_aoi_perf_log_tp_{};
    std::chrono::steady_clock::time_point next_aoi_hotspot_log_tp_{};
    std::filesystem::path aoi_perf_log_path_{};
    std::filesystem::path aoi_summary_log_path_{};
    std::filesystem::path aoi_hotspot_log_path_{};
    std::ofstream aoi_perf_log_stream_{};
    std::ofstream aoi_summary_log_stream_{};
    std::ofstream aoi_hotspot_log_stream_{};
    std::uint32_t aoi_perf_pending_flush_count_ = 0;
    std::uint32_t aoi_summary_pending_flush_count_ = 0;
    std::uint32_t aoi_hotspot_pending_flush_count_ = 0;
    bool enable_zone_aoi_snapshot_ = true;
    bool enable_zone_aoi_move_diff_ = true;
    bool enable_aoi_perf_log_ = true;
    bool enable_aoi_hotspot_log_ = true;
    bool enable_aoi_linear_compare_ = false;
    bool enable_aoi_perf_file_log_ = true;
    bool enable_aoi_summary_file_log_ = true;
    bool enable_aoi_hotspot_file_log_ = true;
    std::unordered_map<std::uint64_t, AoiPerfStats> aoi_perf_stats_by_map_key_{};
};

extern ZoneRuntime g_ZoneMain;

} // namespace svr
