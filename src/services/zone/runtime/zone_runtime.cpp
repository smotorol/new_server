#include "services/zone/runtime/zone_runtime.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <system_error>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <inipp/inipp.h>

#include "server_common/config/runtime_config_path.h"
#include "server_common/config/aoi_config.h"
#include "server_common/data/zone_runtime_data.h"
#include "server_common/session/session_key.h"
#include "services/zone/handler/zone_world_handler.h"
#include "services/world/common/aoi_broadcast_sanitize.h"

namespace svr {
void ZoneRuntime::OnWorldRegisterAckFromHandler(std::uint32_t sid, std::uint32_t serial) { MarkWorldRegistered_(sid, serial); }
void ZoneRuntime::OnWorldDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial) { MarkWorldDisconnected_(sid, serial); }
void ZoneRuntime::OnMapAssignRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZoneMapAssignRequest& req) { OnMapAssignRequest(sid, serial, req); }
void ZoneRuntime::OnPlayerEnterRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerEnter& req) { OnPlayerEnterRequest_(sid, serial, req); }
void ZoneRuntime::OnPlayerLeaveRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerLeave& req) { OnPlayerLeaveRequest_(sid, serial, req); }
void ZoneRuntime::OnPlayerMoveInternalRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerMoveInternal& req) { OnPlayerMoveInternalRequest_(sid, serial, req); }

ZoneRuntime g_ZoneMain;

ZoneRuntime::ZoneRuntime() = default;
ZoneRuntime::~ZoneRuntime() { ReleaseMainThread(); }

std::uint16_t ZoneRuntime::GetActiveMapInstanceCount() const noexcept { return active_map_instance_count_.load(std::memory_order_relaxed); }
std::uint16_t ZoneRuntime::GetMapInstanceCapacity() const noexcept { return map_instance_capacity_; }
std::uint16_t ZoneRuntime::GetLoadScore() const noexcept { return load_score_.load(std::memory_order_relaxed); }
std::uint16_t ZoneRuntime::GetActivePlayerCount() const noexcept { return active_player_count_.load(std::memory_order_relaxed); }

bool ZoneRuntime::OnRuntimeInit()
{
    if (!LoadIniFile_()) {
        spdlog::warn("ZoneRuntime failed to load ini. continuing with defaults.");
    }

    if (!dc::zone::ZoneRuntimeDataStore::LoadFromBinary(dc::zone::DefaultZoneRuntimeBinaryPath())) {
        auto status = dc::zone::ZoneRuntimeDataStore::SnapshotStatus();
        spdlog::warn("ZoneRuntime binary load failed. source={} reason={}", status.source, status.last_error_reason);
    } else {
        auto status = dc::zone::ZoneRuntimeDataStore::SnapshotStatus();
        std::size_t prewarmed = 0;
        if (!served_map_ids_.empty()) {
            for (const auto map_id : served_map_ids_) {
                if (!ValidateZoneMapRuntimeData_(map_id)) {
                    continue;
                }
                EnsureMapInstance_(map_id, 0, true, false);
                ++prewarmed;
            }
        }
        else {
            for (const auto& map : dc::zone::ZoneRuntimeDataStore::GetMapRecords(zone_id_)) {
                EnsureMapInstance_(map.map_id, 0, true, false);
                ++prewarmed;
            }
        }
        spdlog::info("ZoneRuntime binary ready. source={} version={} maps={} portals={} npcs={} monsters={} safe={} special={} prewarmed_maps={} logical_zone_id={} served_maps={}",
            status.source, status.version, status.preload_count, status.portal_count, status.npc_count, status.monster_count, status.safe_count, status.special_count, prewarmed, zone_id_, fmt::join(served_map_ids_, ","));
    }
    return NetworkInit_();
}

bool ZoneRuntime::LoadIniFile_()
{
    namespace fs = std::filesystem;
    const auto ini_path = dc::cfg::ResolveRuntimeConfigPath(
        "DC_ZONE_CONFIG_PATH",
        {
            fs::path("Initialize") / "ZoneSystem.ini",
            fs::path("config") / "zone_server.ini",
            fs::path("ZoneSystem.ini"),
        });

    std::ifstream is(ini_path, std::ios::in | std::ios::binary);
    if (!is) {
        spdlog::warn("ZoneRuntime INI open failed: {}", ini_path.string());
        return false;
    }

    inipp::Ini<char> ini;
    ini.parse(is);

    if (!ini.sections["System"]["ServerName"].empty()) {
        server_name_ = ini.sections["System"]["ServerName"];
    }
    if (!ini.sections["System"]["ZoneServerId"].empty()) {
        zone_server_id_ = static_cast<std::uint32_t>(std::stoul(ini.sections["System"]["ZoneServerId"]));
    }
    if (!ini.sections["System"]["ZoneId"].empty()) {
        zone_id_ = static_cast<std::uint16_t>(std::stoul(ini.sections["System"]["ZoneId"]));
    }
    if (!ini.sections["System"]["WorldId"].empty()) {
        world_id_ = static_cast<std::uint16_t>(std::stoul(ini.sections["System"]["WorldId"]));
    }
    if (!ini.sections["System"]["ChannelId"].empty()) {
        channel_id_ = static_cast<std::uint16_t>(std::stoul(ini.sections["System"]["ChannelId"]));
    }
    if (!ini.sections["World"]["Host"].empty()) {
        world_host_ = ini.sections["World"]["Host"];
    }
    if (!ini.sections["World"]["Port"].empty()) {
        world_port_ = static_cast<std::uint16_t>(std::stoul(ini.sections["World"]["Port"]));
    }
    if (!ini.sections["Capacity"]["MapInstanceCapacity"].empty()) {
        map_instance_capacity_ = static_cast<std::uint16_t>(std::stoul(ini.sections["Capacity"]["MapInstanceCapacity"]));
    }
    if (!ini.sections["Maps"]["ServedMaps"].empty()) {
        served_map_ids_.clear();
        std::stringstream ss(ini.sections["Maps"]["ServedMaps"]);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (item.empty()) {
                continue;
            }
            served_map_ids_.push_back(static_cast<std::uint32_t>(std::stoul(item)));
        }
        std::sort(served_map_ids_.begin(), served_map_ids_.end());
        served_map_ids_.erase(std::unique(served_map_ids_.begin(), served_map_ids_.end()), served_map_ids_.end());
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableZoneAoiSnapshot"].empty()) {
        enable_zone_aoi_snapshot_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableZoneAoiSnapshot"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableZoneAoiMoveDiff"].empty()) {
        enable_zone_aoi_move_diff_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableZoneAoiMoveDiff"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableAoiPerfLog"].empty()) {
        enable_aoi_perf_log_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableAoiPerfLog"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableAoiHotspotLog"].empty()) {
        enable_aoi_hotspot_log_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableAoiHotspotLog"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableAoiLinearCompare"].empty()) {
        enable_aoi_linear_compare_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableAoiLinearCompare"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableAoiPerfFileLog"].empty()) {
        enable_aoi_perf_file_log_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableAoiPerfFileLog"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableAoiSummaryFileLog"].empty()) {
        enable_aoi_summary_file_log_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableAoiSummaryFileLog"]) != 0;
    }
    if (!ini.sections["AOI_TRANSFER"]["EnableAoiHotspotFileLog"].empty()) {
        enable_aoi_hotspot_file_log_ = std::stoi(ini.sections["AOI_TRANSFER"]["EnableAoiHotspotFileLog"]) != 0;
    }

    const auto log_dir = std::filesystem::current_path() / "out" / "servers";
    aoi_perf_log_path_ = log_dir / fmt::format("aoi_perf_zone_{}.csv", zone_server_id_);
    aoi_summary_log_path_ = log_dir / fmt::format("aoi_summary_zone_{}.csv", zone_server_id_);
    aoi_hotspot_log_path_ = log_dir / fmt::format("aoi_hotspot_zone_{}.csv", zone_server_id_);

    spdlog::info(
        "ZoneRuntime INI loaded. file={} server_id={} logical_zone_id={} channel_id={} world_remote={}:{} served_maps={} zone_snapshot={} zone_move_diff={} aoi_perf_log={} aoi_hotspot_log={} aoi_linear_compare={} aoi_perf_file_log={} aoi_summary_file_log={} aoi_hotspot_file_log={}",
        ini_path.string(),
        zone_server_id_,
        zone_id_,
        channel_id_,
        world_host_,
        world_port_,
        fmt::join(served_map_ids_, ","),
        enable_zone_aoi_snapshot_ ? 1 : 0,
        enable_zone_aoi_move_diff_ ? 1 : 0,
        enable_aoi_perf_log_ ? 1 : 0,
        enable_aoi_hotspot_log_ ? 1 : 0,
        enable_aoi_linear_compare_ ? 1 : 0,
        enable_aoi_perf_file_log_ ? 1 : 0,
        enable_aoi_summary_file_log_ ? 1 : 0,
        enable_aoi_hotspot_file_log_ ? 1 : 0);
    return true;
}

void ZoneRuntime::OnBeforeIoStop() {}

void ZoneRuntime::OnAfterIoStop()
{
    world_line_.host.Stop();
    zone_world_handler_.reset();
    world_ready_.store(false, std::memory_order_release);
    world_sid_.store(0, std::memory_order_relaxed);
    world_serial_.store(0, std::memory_order_relaxed);
    if (aoi_perf_log_stream_.is_open()) {
        aoi_perf_log_stream_.flush();
        aoi_perf_log_stream_.close();
    }
    if (aoi_summary_log_stream_.is_open()) {
        aoi_summary_log_stream_.flush();
        aoi_summary_log_stream_.close();
    }
    if (aoi_hotspot_log_stream_.is_open()) {
        aoi_hotspot_log_stream_.flush();
        aoi_hotspot_log_stream_.close();
    }
}

void ZoneRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now)
{
    if (world_ready_.load(std::memory_order_acquire) && now >= next_world_heartbeat_tp_) {
        next_world_heartbeat_tp_ = now + dc::k_next_world_heartbeat_tp;
        SendWorldHeartbeat_();
    }
    if (now >= next_reap_tp_) {
        next_reap_tp_ = now + dc::k_next_reap_tp;
        ReapEmptyDungeonInstances_(now);
    }
    if (enable_aoi_perf_log_ && now >= next_aoi_perf_log_tp_) {
        next_aoi_perf_log_tp_ = now + std::chrono::seconds(5);
        EmitAoiPerfSummaryLogs_(now);
    }
    if (enable_aoi_hotspot_log_ && now >= next_aoi_hotspot_log_tp_) {
        next_aoi_hotspot_log_tp_ = now + std::chrono::seconds(5);
        EmitAoiHotspotLogs_(now);
    }
}

bool ZoneRuntime::NetworkInit_()
{
    zone_world_handler_ = std::make_shared<ZoneWorldHandler>(*this, zone_server_id_, zone_id_, world_id_, channel_id_, server_name_);
    dc::InitOutboundLineEntry(world_line_, 201, "zone-world", world_host_, world_port_, true, 1000, 5000);
    if (!dc::StartOutboundLine(world_line_, io_, zone_world_handler_, [this](std::uint64_t, std::function<void()> fn) { boost::asio::post(io_, std::move(fn)); })) {
        spdlog::error("ZoneRuntime failed to start outbound world line. remote={}:{}", world_host_, world_port_);
        return false;
    }
    spdlog::info("ZoneRuntime started. zone_id={} world_id={} channel_id={} remote_world={}:{}", zone_id_, world_id_, channel_id_, world_host_, world_port_);
    return true;
}

void ZoneRuntime::MarkWorldRegistered_(std::uint32_t sid, std::uint32_t serial)
{
    world_sid_.store(sid, std::memory_order_relaxed);
    world_serial_.store(serial, std::memory_order_relaxed);
    world_ready_.store(true, std::memory_order_release);
    next_world_heartbeat_tp_ = std::chrono::steady_clock::now() + dc::k_next_world_heartbeat_tp;
    next_reap_tp_ = std::chrono::steady_clock::now() + dc::k_next_reap_tp;
    next_aoi_perf_log_tp_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    next_aoi_hotspot_log_tp_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
}

void ZoneRuntime::MarkWorldDisconnected_(std::uint32_t sid, std::uint32_t serial)
{
    const auto cur_sid = world_sid_.load(std::memory_order_relaxed);
    const auto cur_serial = world_serial_.load(std::memory_order_relaxed);
    if (!dc::IsSameSessionKey(cur_sid, cur_serial, sid, serial)) {
        return;
    }
    world_ready_.store(false, std::memory_order_release);
    world_sid_.store(0, std::memory_order_relaxed);
    world_serial_.store(0, std::memory_order_relaxed);
}

void ZoneRuntime::SendWorldHeartbeat_()
{
    if (!zone_world_handler_) {
        return;
    }
    const auto sid = world_sid_.load(std::memory_order_relaxed);
    const auto serial = world_serial_.load(std::memory_order_relaxed);
    if (!dc::IsValidSessionKey(sid, serial)) {
        return;
    }
    if (!zone_world_handler_->SendRouteHeartbeat(0, sid, serial)) {
        spdlog::debug("ZoneRuntime failed to send world heartbeat. sid={} serial={}", sid, serial);
    }
}

void ZoneRuntime::OnMapAssignRequest(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZoneMapAssignRequest& req)
{
    auto* handler = zone_world_handler_.get();
    if (!handler) {
        return;
    }
    if (!ValidateZoneMapRuntimeData_(req.map_template_id)) {
        handler->SendMapAssignResponse(0, sid, serial, req.trace_id, req.request_id,
            static_cast<std::uint16_t>(pt_wz::ZoneMapAssignResultCode::not_found), ResolveGameplayZoneId_(req.map_template_id), req.map_template_id, req.instance_id);
        return;
    }

    const auto key = MakeMapInstanceKey_(req.map_template_id, req.instance_id);
    const bool exists_before = (map_instances_.find(key) != map_instances_.end());

    if (!exists_before && req.create_if_missing == 0) {
        handler->SendMapAssignResponse(0, sid, serial, req.trace_id, req.request_id,
            static_cast<std::uint16_t>(pt_wz::ZoneMapAssignResultCode::not_found), ResolveGameplayZoneId_(req.map_template_id), req.map_template_id, req.instance_id);
        return;
    }

    if (!exists_before && req.create_if_missing != 0 && map_instances_.size() >= static_cast<std::size_t>(map_instance_capacity_)) {
        handler->SendMapAssignResponse(0, sid, serial, req.trace_id, req.request_id,
            static_cast<std::uint16_t>(pt_wz::ZoneMapAssignResultCode::capacity_full), ResolveGameplayZoneId_(req.map_template_id), req.map_template_id, req.instance_id);
        return;
    }

    EnsureMapInstance_(req.map_template_id, req.instance_id, req.create_if_missing != 0, req.dungeon_instance != 0);
    handler->SendMapAssignResponse(0, sid, serial, req.trace_id, req.request_id,
        static_cast<std::uint16_t>(pt_wz::ZoneMapAssignResultCode::ok), ResolveGameplayZoneId_(req.map_template_id), req.map_template_id, req.instance_id);
}

void ZoneRuntime::EnsureMapInstance_(std::uint32_t map_template_id, std::uint32_t instance_id, bool create_if_missing, bool dungeon_instance)
{
    const auto key = MakeMapInstanceKey_(map_template_id, instance_id);
    const auto now = std::chrono::steady_clock::now();
    auto it = map_instances_.find(key);
    if (it != map_instances_.end()) {
        it->second.last_access_at = now;
        return;
    }
    if (!create_if_missing) {
        return;
    }
    MapInstanceState st{};
    st.map_template_id = map_template_id;
    st.instance_id = instance_id;
    st.dungeon_instance = dungeon_instance || IsDungeonMapTemplate_(map_template_id);
    st.created_at = now;
    st.last_access_at = now;
    map_instances_[key] = st;
    RefreshMetrics_();
}

void ZoneRuntime::OnPlayerEnterRequest_(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerEnter& req)
{
    auto* handler = zone_world_handler_.get();
    if (!handler) {
        return;
    }
    const auto key = MakeMapInstanceKey_(req.map_template_id, req.instance_id);
    auto it = map_instances_.find(key);
    if (it == map_instances_.end()) {
        handler->SendPlayerEnterAck(0, sid, serial, req.trace_id, req.request_id,
            static_cast<std::uint16_t>(pt_wz::ZonePlayerEnterResultCode::map_not_found), ResolveGameplayZoneId_(req.map_template_id), req.char_id, req.map_template_id, req.instance_id);
        return;
    }

    auto bind_it = player_bindings_.find(req.char_id);
    if (bind_it != player_bindings_.end()) {
        if (bind_it->second.map_key == key) {
            it->second.last_access_at = std::chrono::steady_clock::now();
            handler->SendPlayerEnterAck(0, sid, serial, req.trace_id, req.request_id,
                static_cast<std::uint16_t>(pt_wz::ZonePlayerEnterResultCode::ok), ResolveGameplayZoneId_(req.map_template_id), req.char_id, req.map_template_id, req.instance_id);
            return;
        }
        auto old_it = map_instances_.find(bind_it->second.map_key);
        if (old_it != map_instances_.end() && old_it->second.active_player_count > 0) {
            --old_it->second.active_player_count;
            old_it->second.last_access_at = std::chrono::steady_clock::now();
        }
    }

    ++it->second.active_player_count;
    it->second.last_access_at = std::chrono::steady_clock::now();
    PlayerBindingState binding{};
    binding.map_key = key;
    binding.map_template_id = req.map_template_id;
    binding.instance_id = req.instance_id;
    if (bind_it != player_bindings_.end()) {
        if (bind_it->second.has_position && bind_it->second.map_key != key) {
            RemoveActorFromSpatialIndex_(bind_it->second.map_key, req.char_id, bind_it->second.cell_x, bind_it->second.cell_y);
        }
        binding.sid = bind_it->second.sid;
        binding.serial = bind_it->second.serial;
        binding.x = bind_it->second.x;
        binding.y = bind_it->second.y;
        binding.cell_x = bind_it->second.cell_x;
        binding.cell_y = bind_it->second.cell_y;
        binding.has_position = bind_it->second.has_position;
    }
    player_bindings_[req.char_id] = binding;
    RefreshMetrics_();
    LogZoneRegionSummary_(req.map_template_id, 0, 0);

    handler->SendPlayerEnterAck(0, sid, serial, req.trace_id, req.request_id,
        static_cast<std::uint16_t>(pt_wz::ZonePlayerEnterResultCode::ok), ResolveGameplayZoneId_(req.map_template_id), req.char_id, req.map_template_id, req.instance_id);
}

void ZoneRuntime::OnPlayerLeaveRequest_(std::uint32_t, std::uint32_t, const pt_wz::WorldZonePlayerLeave& req)
{
    auto bind_it = player_bindings_.find(req.char_id);
    if (bind_it == player_bindings_.end()) {
        return;
    }
    const auto leaving_state = bind_it->second;
    if (leaving_state.has_position && zone_world_handler_) {
        const auto world_sid = world_sid_.load(std::memory_order_relaxed);
        const auto world_serial = world_serial_.load(std::memory_order_relaxed);
        if (dc::IsValidSessionKey(world_sid, world_serial)) {
            const auto visible_ids = BuildVisibleActorIdsForPosition_(req.char_id, leaving_state.map_key, leaving_state.x, leaving_state.y);
            proto::S2C_player_despawn_item self_item{};
            self_item.char_id = req.char_id;
            for (const auto other_char_id : visible_ids) {
                const auto other_it = player_bindings_.find(other_char_id);
                if (other_it == player_bindings_.end() || !dc::IsValidSessionKey(other_it->second.sid, other_it->second.serial)) {
                    continue;
                }
                zone_world_handler_->SendAoiDespawnBatch(
                    0,
                    world_sid,
                    world_serial,
                    0,
                    other_it->second.sid,
                    other_it->second.serial,
                    other_char_id,
                    leaving_state.map_template_id,
                    leaving_state.instance_id,
                    ResolveGameplayZoneId_(leaving_state.map_template_id),
                    channel_id_,
                    std::vector<proto::S2C_player_despawn_item>{ self_item });
            }
            spdlog::info(
                "ZoneRuntime leave despawn relayed. zone_server_id={} char_id={} map={} channel={} instance={} receiver_count={}",
                zone_server_id_,
                req.char_id,
                leaving_state.map_template_id,
                channel_id_,
                leaving_state.instance_id,
                visible_ids.size());
        }
    }
    const auto key = bind_it->second.map_key;
    if (bind_it->second.has_position) {
        RemoveActorFromSpatialIndex_(bind_it->second.map_key, req.char_id, bind_it->second.cell_x, bind_it->second.cell_y);
    }
    auto it = map_instances_.find(key);
    if (it != map_instances_.end()) {
        if (it->second.active_player_count > 0) {
            --it->second.active_player_count;
        }
        it->second.last_access_at = std::chrono::steady_clock::now();
    }
    player_bindings_.erase(bind_it);
    RefreshMetrics_();
}

void ZoneRuntime::OnPlayerMoveInternalRequest_(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerMoveInternal& req)
{
    const auto move_started_at = std::chrono::high_resolution_clock::now();
    auto bind_it = player_bindings_.find(req.char_id);
    if (bind_it == player_bindings_.end()) {
        spdlog::warn(
            "ZoneRuntime AOI move stub received for unbound player. zone_server_id={} sid={} serial={} char_id={} map={} channel={} instance={} pos=({}, {})",
            zone_server_id_,
            sid,
            serial,
            req.char_id,
            req.map_template_id,
            req.channel_id,
            req.instance_id,
            req.x,
            req.y);
        return;
    }

    const auto before_state = bind_it->second;
    std::size_t before_candidate_count = 0;
    std::size_t before_neighbor_cell_count = 0;
    std::uint64_t visible_calc_time_us = 0;
    std::uint64_t diff_calc_time_us = 0;
    std::uint64_t spawn_build_time_us = 0;
    std::uint64_t despawn_build_time_us = 0;
    std::uint64_t move_build_time_us = 0;
    const auto before_visible_started_at = std::chrono::high_resolution_clock::now();
    const auto before_visible = (enable_zone_aoi_move_diff_ && req.request_id == 0 && before_state.has_position)
        ? BuildVisibleActorIdsForPosition_(req.char_id, before_state.map_key, before_state.x, before_state.y, &before_candidate_count, &before_neighbor_cell_count)
        : std::vector<std::uint64_t>{};
    visible_calc_time_us += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - before_visible_started_at).count());

    const auto map_it = map_instances_.find(bind_it->second.map_key);
    if (map_it != map_instances_.end()) {
        map_it->second.last_access_at = std::chrono::steady_clock::now();
    }

    bind_it->second.sid = req.sid;
    bind_it->second.serial = req.serial;
    bind_it->second.x = req.x;
    bind_it->second.y = req.y;
    bind_it->second.cell_x = ResolveSpatialCellCoord_(req.x, ResolveAoiCellSize_());
    bind_it->second.cell_y = ResolveSpatialCellCoord_(req.y, ResolveAoiCellSize_());
    bind_it->second.has_position = true;
    UpdateActorSpatialIndex_(bind_it->second.map_key, req.char_id, before_state, bind_it->second);

    spdlog::info(
        "ZoneRuntime AOI move mirrored. zone_server_id={} sid={} serial={} char_id={} map={} channel={} instance={} pos=({}, {}) trace_id={} request_id={}",
        zone_server_id_,
        sid,
        serial,
        req.char_id,
        req.map_template_id,
        req.channel_id,
        req.instance_id,
        req.x,
        req.y,
        req.trace_id,
        req.request_id);

    if (enable_zone_aoi_snapshot_ && req.request_id != 0) {
        if (!TrySendInitialAoiSnapshot_(req.trace_id, req.char_id, bind_it->second, req.request_id)) {
            spdlog::warn(
                "ZoneRuntime initial snapshot send skipped. zone_server_id={} char_id={} map={} channel={} instance={} sid={} serial={} has_position={}",
                zone_server_id_,
                req.char_id,
                req.map_template_id,
                req.channel_id,
                req.instance_id,
                req.sid,
                req.serial,
                bind_it->second.has_position ? 1 : 0);
        }
        return;
    }

    if (!enable_zone_aoi_move_diff_ || !zone_world_handler_) {
        return;
    }

    std::size_t after_candidate_count = 0;
    std::size_t after_neighbor_cell_count = 0;
    const auto after_visible_started_at = std::chrono::high_resolution_clock::now();
    const auto after_visible = BuildVisibleActorIdsForPosition_(
        req.char_id,
        bind_it->second.map_key,
        bind_it->second.x,
        bind_it->second.y,
        &after_candidate_count,
        &after_neighbor_cell_count);
    visible_calc_time_us += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - after_visible_started_at).count());
    auto before_sorted = before_visible;
    auto after_sorted = after_visible;
    const auto diff_started_at = std::chrono::high_resolution_clock::now();
    std::sort(before_sorted.begin(), before_sorted.end());
    std::sort(after_sorted.begin(), after_sorted.end());

    std::vector<std::uint64_t> entered;
    std::vector<std::uint64_t> exited;
    std::vector<std::uint64_t> persisted;
    std::set_difference(after_sorted.begin(), after_sorted.end(), before_sorted.begin(), before_sorted.end(), std::back_inserter(entered));
    std::set_difference(before_sorted.begin(), before_sorted.end(), after_sorted.begin(), after_sorted.end(), std::back_inserter(exited));
    std::set_intersection(before_sorted.begin(), before_sorted.end(), after_sorted.begin(), after_sorted.end(), std::back_inserter(persisted));
    diff_calc_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - diff_started_at).count());

    if (enable_aoi_linear_compare_) {
        const auto full_scan_started_at = std::chrono::high_resolution_clock::now();
        std::vector<std::uint64_t> linear_visible;
        const auto& cfg = dc::cfg::GetAoiConfig();
        const auto radius_m = static_cast<std::int64_t>(cfg.world_sight_unit) * static_cast<std::int64_t>(cfg.aoi_radius_cells);
        const auto radius_sq = radius_m * radius_m;
        for (const auto& [other_char_id, other] : player_bindings_) {
            if (other_char_id == req.char_id || other.map_key != bind_it->second.map_key || !other.has_position) {
                continue;
            }
            const auto dx = static_cast<std::int64_t>(other.x) - static_cast<std::int64_t>(bind_it->second.x);
            const auto dy = static_cast<std::int64_t>(other.y) - static_cast<std::int64_t>(bind_it->second.y);
            if ((dx * dx + dy * dy) <= radius_sq) {
                linear_visible.push_back(other_char_id);
            }
        }
        std::sort(linear_visible.begin(), linear_visible.end());
        if (linear_visible != after_sorted) {
            spdlog::warn(
                "ZoneRuntime AOI linear compare mismatch. zone_server_id={} char_id={} map_key={} spatial_visible={} linear_visible={} compare_time_us={}",
                zone_server_id_,
                req.char_id,
                bind_it->second.map_key,
                after_sorted.size(),
                linear_visible.size(),
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - full_scan_started_at).count());
        }
    }

    spdlog::info(
        "ZoneRuntime AOI move diff. zone_server_id={} char_id={} map={} channel={} instance={} occupied_cells={} before_visible={} after_visible={} entered={} exited={} moved={} before_candidates={} after_candidates={} after_neighbor_cells={} visible_calc_us={} diff_calc_us={}",
        zone_server_id_,
        req.char_id,
        req.map_template_id,
        req.channel_id,
        req.instance_id,
        [&]() -> std::size_t {
            const auto index_it = spatial_index_by_map_key_.find(bind_it->second.map_key);
            return index_it != spatial_index_by_map_key_.end() ? index_it->second.cells.size() : 0;
        }(),
        before_sorted.size(),
        after_sorted.size(),
        entered.size(),
        exited.size(),
        persisted.size(),
        before_candidate_count,
        after_candidate_count,
        after_neighbor_cell_count,
        visible_calc_time_us,
        diff_calc_time_us);

    const auto world_sid = world_sid_.load(std::memory_order_relaxed);
    const auto world_serial = world_serial_.load(std::memory_order_relaxed);
    if (!dc::IsValidSessionKey(world_sid, world_serial)) {
        return;
    }

    if (!entered.empty() && dc::IsValidSessionKey(req.sid, req.serial)) {
        const auto spawn_build_started_at = std::chrono::high_resolution_clock::now();
        const auto items = BuildSpawnItemsForActorIds_(entered);
        spawn_build_time_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - spawn_build_started_at).count());
        if (!items.empty()) {
            zone_world_handler_->SendAoiSpawnBatch(0, world_sid, world_serial, req.trace_id, req.sid, req.serial, req.char_id, req.map_template_id, req.instance_id, ResolveGameplayZoneId_(req.map_template_id), channel_id_, items);
        }
    }
    if (!exited.empty() && dc::IsValidSessionKey(req.sid, req.serial)) {
        const auto despawn_build_started_at = std::chrono::high_resolution_clock::now();
        std::vector<proto::S2C_player_despawn_item> items;
        items.reserve(exited.size());
        for (const auto other_char_id : exited) {
            proto::S2C_player_despawn_item item{};
            item.char_id = other_char_id;
            items.push_back(item);
        }
        despawn_build_time_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - despawn_build_started_at).count());
        if (!items.empty()) {
            zone_world_handler_->SendAoiDespawnBatch(0, world_sid, world_serial, req.trace_id, req.sid, req.serial, req.char_id, req.map_template_id, req.instance_id, ResolveGameplayZoneId_(req.map_template_id), channel_id_, items);
        }
    }
    for (const auto other_char_id : entered) {
        const auto other_it = player_bindings_.find(other_char_id);
        if (other_it == player_bindings_.end() || !dc::IsValidSessionKey(other_it->second.sid, other_it->second.serial)) {
            continue;
        }
        proto::S2C_player_spawn_item item{};
        item.char_id = req.char_id;
        item.x = req.x;
        item.y = req.y;
        const auto spawn_build_started_at = std::chrono::high_resolution_clock::now();
        zone_world_handler_->SendAoiSpawnBatch(0, world_sid, world_serial, req.trace_id, other_it->second.sid, other_it->second.serial, other_char_id, req.map_template_id, req.instance_id, ResolveGameplayZoneId_(req.map_template_id), channel_id_, std::vector<proto::S2C_player_spawn_item>{ item });
        spawn_build_time_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - spawn_build_started_at).count());
    }
    for (const auto other_char_id : exited) {
        const auto other_it = player_bindings_.find(other_char_id);
        if (other_it == player_bindings_.end() || !dc::IsValidSessionKey(other_it->second.sid, other_it->second.serial)) {
            continue;
        }
        proto::S2C_player_despawn_item item{};
        item.char_id = req.char_id;
        const auto despawn_build_started_at = std::chrono::high_resolution_clock::now();
        zone_world_handler_->SendAoiDespawnBatch(0, world_sid, world_serial, req.trace_id, other_it->second.sid, other_it->second.serial, other_char_id, req.map_template_id, req.instance_id, ResolveGameplayZoneId_(req.map_template_id), channel_id_, std::vector<proto::S2C_player_despawn_item>{ item });
        despawn_build_time_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - despawn_build_started_at).count());
    }
    const auto move_batch_started_at = std::chrono::high_resolution_clock::now();
    for (const auto other_char_id : persisted) {
        const auto other_it = player_bindings_.find(other_char_id);
        if (other_it == player_bindings_.end() || !dc::IsValidSessionKey(other_it->second.sid, other_it->second.serial)) {
            continue;
        }
        proto::S2C_player_move_item item{};
        item.char_id = req.char_id;
        item.x = req.x;
        item.y = req.y;
        zone_world_handler_->SendAoiMoveBatch(0, world_sid, world_serial, req.trace_id, other_it->second.sid, other_it->second.serial, other_char_id, req.map_template_id, req.instance_id, ResolveGameplayZoneId_(req.map_template_id), channel_id_, std::vector<proto::S2C_player_move_item>{ item });
    }
    move_build_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - move_batch_started_at).count());

    const auto total_move_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - move_started_at).count());
    RecordAoiPerf_(
        bind_it->second.map_key,
        req.char_id,
        after_candidate_count,
        before_sorted.size(),
        after_sorted.size(),
        [&]() -> std::size_t {
            const auto index_it = spatial_index_by_map_key_.find(bind_it->second.map_key);
            return index_it != spatial_index_by_map_key_.end() ? index_it->second.cells.size() : 0;
        }(),
        after_neighbor_cell_count,
        visible_calc_time_us,
        diff_calc_time_us,
        spawn_build_time_us,
        despawn_build_time_us,
        move_build_time_us,
        total_move_time_us);
}

std::vector<std::uint64_t> ZoneRuntime::BuildVisibleActorIdsForPosition_(
    std::uint64_t char_id,
    std::uint64_t map_key,
    std::int32_t x,
    std::int32_t y,
    std::size_t* out_candidate_count,
    std::size_t* out_neighbor_cell_count) const
{
    std::vector<std::uint64_t> ids;
    const auto& cfg = dc::cfg::GetAoiConfig();
    const auto cell_size = ResolveAoiCellSize_();
    const auto radius_m = static_cast<std::int64_t>(cfg.world_sight_unit) * static_cast<std::int64_t>(cfg.aoi_radius_cells);
    const auto radius_sq = radius_m * radius_m;
    const auto query_cell_x = ResolveSpatialCellCoord_(x, cell_size);
    const auto query_cell_y = ResolveSpatialCellCoord_(y, cell_size);
    const auto neighbor_range = std::max<std::int32_t>(1, static_cast<std::int32_t>((radius_m + cell_size - 1) / cell_size));
    std::unordered_set<std::uint64_t> candidate_ids;
    candidate_ids.reserve(64);
    std::size_t neighbor_cell_count = 0;

    if (const auto index_it = spatial_index_by_map_key_.find(map_key); index_it != spatial_index_by_map_key_.end()) {
        for (std::int32_t cell_y = query_cell_y - neighbor_range; cell_y <= query_cell_y + neighbor_range; ++cell_y) {
            for (std::int32_t cell_x = query_cell_x - neighbor_range; cell_x <= query_cell_x + neighbor_range; ++cell_x) {
                ++neighbor_cell_count;
                const auto cell_it = index_it->second.cells.find(MakeSpatialCellKey_(cell_x, cell_y));
                if (cell_it == index_it->second.cells.end()) {
                    continue;
                }
                for (const auto other_char_id : cell_it->second) {
                    candidate_ids.insert(other_char_id);
                }
            }
        }
    }

    if (out_candidate_count) {
        *out_candidate_count = candidate_ids.size();
    }
    if (out_neighbor_cell_count) {
        *out_neighbor_cell_count = neighbor_cell_count;
    }

    for (const auto other_char_id : candidate_ids) {
        const auto it = player_bindings_.find(other_char_id);
        if (it == player_bindings_.end()) {
            continue;
        }
        const auto& other = it->second;
        if (other_char_id == char_id) {
            continue;
        }
        if (other.map_key != map_key || !other.has_position) {
            continue;
        }

        const auto dx = static_cast<std::int64_t>(other.x) - static_cast<std::int64_t>(x);
        const auto dy = static_cast<std::int64_t>(other.y) - static_cast<std::int64_t>(y);
        if ((dx * dx + dy * dy) > radius_sq) {
            continue;
        }
        ids.push_back(other_char_id);
    }

    return svr::aoi::SanitizeEntityIds(ids);
}

std::int32_t ZoneRuntime::ResolveAoiCellSize_() noexcept
{
    const auto& cfg = dc::cfg::GetAoiConfig();
    const auto radius_m = std::max(1, cfg.world_sight_unit * cfg.aoi_radius_cells);
    return std::max(1, radius_m / 2);
}

std::uint64_t ZoneRuntime::MakeSpatialCellKey_(std::int32_t cell_x, std::int32_t cell_y) noexcept
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_x)) << 32) |
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_y));
}

std::int32_t ZoneRuntime::ResolveSpatialCellCoord_(std::int32_t value, std::int32_t cell_size) noexcept
{
    if (cell_size <= 0) {
        return 0;
    }
    const auto numerator = static_cast<std::int64_t>(value);
    const auto denominator = static_cast<std::int64_t>(cell_size);
    auto q = numerator / denominator;
    auto r = numerator % denominator;
    if (r != 0 && ((r > 0) != (denominator > 0))) {
        --q;
    }
    return static_cast<std::int32_t>(q);
}

void ZoneRuntime::InsertActorIntoSpatialIndex_(std::uint64_t map_key, std::uint64_t char_id, std::int32_t cell_x, std::int32_t cell_y)
{
    auto& bucket = spatial_index_by_map_key_[map_key].cells[MakeSpatialCellKey_(cell_x, cell_y)];
    bucket.insert(char_id);
}

void ZoneRuntime::RemoveActorFromSpatialIndex_(std::uint64_t map_key, std::uint64_t char_id, std::int32_t cell_x, std::int32_t cell_y)
{
    const auto map_it = spatial_index_by_map_key_.find(map_key);
    if (map_it == spatial_index_by_map_key_.end()) {
        return;
    }
    const auto cell_key = MakeSpatialCellKey_(cell_x, cell_y);
    const auto cell_it = map_it->second.cells.find(cell_key);
    if (cell_it == map_it->second.cells.end()) {
        return;
    }
    auto& bucket = cell_it->second;
    bucket.erase(char_id);
    if (bucket.empty()) {
        map_it->second.cells.erase(cell_it);
    }
    if (map_it->second.cells.empty()) {
        spatial_index_by_map_key_.erase(map_it);
    }
}

void ZoneRuntime::UpdateActorSpatialIndex_(
    std::uint64_t map_key,
    std::uint64_t char_id,
    const PlayerBindingState& before_state,
    const PlayerBindingState& after_state)
{
    if (!after_state.has_position) {
        return;
    }
    if (!before_state.has_position || before_state.map_key != map_key) {
        InsertActorIntoSpatialIndex_(map_key, char_id, after_state.cell_x, after_state.cell_y);
        return;
    }
    if (before_state.cell_x == after_state.cell_x && before_state.cell_y == after_state.cell_y) {
        return;
    }
    RemoveActorFromSpatialIndex_(map_key, char_id, before_state.cell_x, before_state.cell_y);
    InsertActorIntoSpatialIndex_(map_key, char_id, after_state.cell_x, after_state.cell_y);
}

std::vector<proto::S2C_player_spawn_item> ZoneRuntime::BuildVisibleSnapshotItems_(
    std::uint64_t char_id,
    const PlayerBindingState& self_state) const
{
    return BuildSpawnItemsForActorIds_(BuildVisibleActorIdsForPosition_(char_id, self_state.map_key, self_state.x, self_state.y));
}

std::vector<proto::S2C_player_spawn_item> ZoneRuntime::BuildSpawnItemsForActorIds_(
    const std::vector<std::uint64_t>& actor_ids) const
{
    std::vector<proto::S2C_player_spawn_item> items;
    items.reserve(actor_ids.size());
    for (const auto actor_id : actor_ids) {
        const auto it = player_bindings_.find(actor_id);
        if (it == player_bindings_.end() || !it->second.has_position) {
            continue;
        }
        proto::S2C_player_spawn_item item{};
        item.char_id = actor_id;
        item.x = it->second.x;
        item.y = it->second.y;
        items.push_back(item);
    }
    const auto count = svr::aoi::ClampBatchEntityCount(items.size());
    items.resize(count);
    return items;
}

bool ZoneRuntime::TrySendInitialAoiSnapshot_(
    std::uint64_t trace_id,
    std::uint64_t char_id,
    const PlayerBindingState& self_state,
    std::uint64_t snapshot_request_id)
{
    if (!zone_world_handler_ || !self_state.has_position) {
        return false;
    }
    if (!dc::IsValidSessionKey(self_state.sid, self_state.serial)) {
        return false;
    }

    const auto items = BuildVisibleSnapshotItems_(char_id, self_state);
    const auto sid = world_sid_.load(std::memory_order_relaxed);
    const auto serial = world_serial_.load(std::memory_order_relaxed);
    if (!dc::IsValidSessionKey(sid, serial)) {
        return false;
    }

    const bool sent = zone_world_handler_->SendAoiSnapshot(
        0,
        sid,
        serial,
        trace_id,
        self_state.sid,
        self_state.serial,
        char_id,
        self_state.map_template_id,
        self_state.instance_id,
        ResolveGameplayZoneId_(self_state.map_template_id),
        channel_id_,
        self_state.x,
        self_state.y,
        items);

    if (sent) {
        // request_id: 1=enter, 2=portal, 3=reconnect
        const bool notify_visible_receivers = snapshot_request_id != 3;
        if (notify_visible_receivers) {
            proto::S2C_player_spawn_item self_item{};
            self_item.char_id = char_id;
            self_item.x = self_state.x;
            self_item.y = self_state.y;
            const auto visible_ids = BuildVisibleActorIdsForPosition_(char_id, self_state.map_key, self_state.x, self_state.y);
            for (const auto other_char_id : visible_ids) {
                const auto other_it = player_bindings_.find(other_char_id);
                if (other_it == player_bindings_.end() || !dc::IsValidSessionKey(other_it->second.sid, other_it->second.serial)) {
                    continue;
                }
                zone_world_handler_->SendAoiSpawnBatch(
                    0,
                    sid,
                    serial,
                    trace_id,
                    other_it->second.sid,
                    other_it->second.serial,
                    other_char_id,
                    self_state.map_template_id,
                    self_state.instance_id,
                    ResolveGameplayZoneId_(self_state.map_template_id),
                    channel_id_,
                    std::vector<proto::S2C_player_spawn_item>{ self_item });
            }
        }
        spdlog::info(
            "ZoneRuntime initial snapshot sent. zone_server_id={} char_id={} map={} channel={} instance={} self=({}, {}) visible_count={} request_id={}",
            zone_server_id_,
            char_id,
            self_state.map_template_id,
            channel_id_,
            self_state.instance_id,
            self_state.x,
            self_state.y,
            items.size(),
            snapshot_request_id);
    }
    return sent;
}

void ZoneRuntime::RecordAoiPerf_(
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
    std::uint64_t total_move_time_us)
{
    if (enable_aoi_perf_file_log_ || enable_aoi_summary_file_log_ || enable_aoi_hotspot_file_log_) {
        EnsureAoiLogFilesReady_();
    }
    auto& stats = aoi_perf_stats_by_map_key_[map_key];
    ++stats.move_count;
    stats.visible_calc_total_us += visible_calc_time_us;
    stats.visible_calc_max_us = std::max(stats.visible_calc_max_us, visible_calc_time_us);
    stats.diff_calc_total_us += diff_calc_time_us;
    stats.diff_calc_max_us = std::max(stats.diff_calc_max_us, diff_calc_time_us);
    const auto batch_build_time_us = spawn_build_time_us + despawn_build_time_us + move_build_time_us;
    stats.batch_build_total_us += batch_build_time_us;
    stats.batch_build_max_us = std::max(stats.batch_build_max_us, batch_build_time_us);
    stats.total_move_total_us += total_move_time_us;
    stats.total_move_max_us = std::max(stats.total_move_max_us, total_move_time_us);
    stats.candidate_total += candidate_count;
    stats.candidate_max = std::max<std::uint64_t>(stats.candidate_max, candidate_count);
    stats.visible_after_total += visible_after_count;
    stats.visible_after_max = std::max<std::uint64_t>(stats.visible_after_max, visible_after_count);
    stats.recent_total_move_us.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(total_move_time_us, std::numeric_limits<std::uint32_t>::max())));
    constexpr std::size_t kRecentWindow = 256;
    if (stats.recent_total_move_us.size() > kRecentWindow) {
        stats.recent_total_move_us.pop_front();
    }

    if (enable_aoi_perf_log_) {
        spdlog::info(
            "ZoneRuntime AOI perf sample. zone_server_id={} map_key={} actor_id={} candidate_count={} visible_before={} visible_after={} occupied_cells={} neighbor_cells={} visible_calc_time_us={} diff_calc_time_us={} spawn_build_time_us={} despawn_build_time_us={} move_build_time_us={} total_move_time_us={}",
            zone_server_id_,
            map_key,
            char_id,
            candidate_count,
            visible_before_count,
            visible_after_count,
            occupied_cells,
            neighbor_cell_count,
            visible_calc_time_us,
            diff_calc_time_us,
            spawn_build_time_us,
            despawn_build_time_us,
            move_build_time_us,
            total_move_time_us);
    }
    if (enable_aoi_perf_file_log_ && aoi_perf_log_stream_.is_open()) {
        RotateAoiLogFileIfNeeded_(aoi_perf_log_stream_, aoi_perf_log_path_, "timestamp_ms,map_key,actor_id,candidate_count,visible_before,visible_after,occupied_cells,neighbor_cells,visible_calc_time_us,diff_calc_time_us,spawn_build_time_us,despawn_build_time_us,move_build_time_us,total_move_time_us");
        aoi_perf_log_stream_
            << CurrentUnixTimestampMs_() << ','
            << map_key << ','
            << char_id << ','
            << candidate_count << ','
            << visible_before_count << ','
            << visible_after_count << ','
            << occupied_cells << ','
            << neighbor_cell_count << ','
            << visible_calc_time_us << ','
            << diff_calc_time_us << ','
            << spawn_build_time_us << ','
            << despawn_build_time_us << ','
            << move_build_time_us << ','
            << total_move_time_us
            << '\n';
        if (++aoi_perf_pending_flush_count_ >= 64) {
            aoi_perf_log_stream_.flush();
            aoi_perf_pending_flush_count_ = 0;
        }
    }
}

void ZoneRuntime::EmitAoiPerfSummaryLogs_(std::chrono::steady_clock::time_point now)
{
    next_aoi_perf_log_tp_ = now + std::chrono::seconds(5);
    if (enable_aoi_summary_file_log_) {
        EnsureAoiLogFilesReady_();
    }
    if (!enable_aoi_perf_log_) {
        if (!enable_aoi_summary_file_log_) {
            return;
        }
    }

    for (const auto& [map_key, stats] : aoi_perf_stats_by_map_key_) {
        if (stats.move_count == 0) {
            continue;
        }
        std::uint32_t p95_total_move_us = 0;
        if (!stats.recent_total_move_us.empty()) {
            std::vector<std::uint32_t> sorted(stats.recent_total_move_us.begin(), stats.recent_total_move_us.end());
            std::sort(sorted.begin(), sorted.end());
            const auto index = std::min<std::size_t>(sorted.size() - 1, static_cast<std::size_t>((sorted.size() * 95) / 100));
            p95_total_move_us = sorted[index];
        }
        spdlog::info(
            "ZoneRuntime AOI perf summary. zone_server_id={} map_key={} moves={} avg_visible_calc_us={} max_visible_calc_us={} avg_diff_calc_us={} max_diff_calc_us={} avg_batch_build_us={} max_batch_build_us={} avg_total_move_us={} p95_total_move_us={} max_total_move_us={} avg_candidate_count={} max_candidate_count={} avg_visible_after={} max_visible_after={}",
            zone_server_id_,
            map_key,
            stats.move_count,
            stats.visible_calc_total_us / stats.move_count,
            stats.visible_calc_max_us,
            stats.diff_calc_total_us / stats.move_count,
            stats.diff_calc_max_us,
            stats.batch_build_total_us / stats.move_count,
            stats.batch_build_max_us,
            stats.total_move_total_us / stats.move_count,
            p95_total_move_us,
            stats.total_move_max_us,
            stats.candidate_total / stats.move_count,
            stats.candidate_max,
            stats.visible_after_total / stats.move_count,
            stats.visible_after_max);
        if (enable_aoi_summary_file_log_ && aoi_summary_log_stream_.is_open()) {
            RotateAoiLogFileIfNeeded_(aoi_summary_log_stream_, aoi_summary_log_path_, "timestamp_ms,map_key,moves,avg_visible_calc_us,max_visible_calc_us,avg_diff_calc_us,max_diff_calc_us,avg_batch_build_us,max_batch_build_us,avg_total_move_us,p95_total_move_us,max_total_move_us,avg_candidate_count,max_candidate_count,avg_visible_after,max_visible_after,actor_count");
            std::size_t actor_count = 0;
            for (const auto& [_, binding] : player_bindings_) {
                if (binding.map_key == map_key) {
                    ++actor_count;
                }
            }
            aoi_summary_log_stream_
                << CurrentUnixTimestampMs_() << ','
                << map_key << ','
                << stats.move_count << ','
                << (stats.visible_calc_total_us / stats.move_count) << ','
                << stats.visible_calc_max_us << ','
                << (stats.diff_calc_total_us / stats.move_count) << ','
                << stats.diff_calc_max_us << ','
                << (stats.batch_build_total_us / stats.move_count) << ','
                << stats.batch_build_max_us << ','
                << (stats.total_move_total_us / stats.move_count) << ','
                << p95_total_move_us << ','
                << stats.total_move_max_us << ','
                << (stats.candidate_total / stats.move_count) << ','
                << stats.candidate_max << ','
                << (stats.visible_after_total / stats.move_count) << ','
                << stats.visible_after_max << ','
                << actor_count
                << '\n';
            if (++aoi_summary_pending_flush_count_ >= 4) {
                aoi_summary_log_stream_.flush();
                aoi_summary_pending_flush_count_ = 0;
            }
        }
    }
}

void ZoneRuntime::EmitAoiHotspotLogs_(std::chrono::steady_clock::time_point now)
{
    next_aoi_hotspot_log_tp_ = now + std::chrono::seconds(5);
    if (enable_aoi_hotspot_file_log_) {
        EnsureAoiLogFilesReady_();
    }
    if (!enable_aoi_hotspot_log_) {
        if (!enable_aoi_hotspot_file_log_) {
            return;
        }
    }

    for (const auto& [map_key, index] : spatial_index_by_map_key_) {
        struct CellLoad {
            std::uint64_t cell_key = 0;
            std::size_t actor_count = 0;
        };
        std::vector<CellLoad> loads;
        loads.reserve(index.cells.size());
        for (const auto& [cell_key, bucket] : index.cells) {
            loads.push_back(CellLoad{ cell_key, bucket.size() });
        }
        std::sort(loads.begin(), loads.end(), [](const CellLoad& lhs, const CellLoad& rhs) {
            if (lhs.actor_count != rhs.actor_count) {
                return lhs.actor_count > rhs.actor_count;
            }
            return lhs.cell_key < rhs.cell_key;
        });
        constexpr std::size_t kTopN = 3;
        for (std::size_t i = 0; i < std::min<std::size_t>(kTopN, loads.size()); ++i) {
            const auto cell_x = DecodeSpatialCellX_(loads[i].cell_key);
            const auto cell_y = DecodeSpatialCellY_(loads[i].cell_key);
            spdlog::info(
                "ZoneRuntime AOI hotspot cell. zone_server_id={} map_key={} rank={} cell=({}, {}) actor_count={} occupied_cells={}",
                zone_server_id_,
                map_key,
                i + 1,
                cell_x,
                cell_y,
                loads[i].actor_count,
                index.cells.size());
            if (enable_aoi_hotspot_file_log_ && aoi_hotspot_log_stream_.is_open()) {
                RotateAoiLogFileIfNeeded_(aoi_hotspot_log_stream_, aoi_hotspot_log_path_, "timestamp_ms,map_key,rank,cell_key,cell_x,cell_y,actor_count,occupied_cells");
                aoi_hotspot_log_stream_
                    << CurrentUnixTimestampMs_() << ','
                    << map_key << ','
                    << (i + 1) << ','
                    << loads[i].cell_key << ','
                    << cell_x << ','
                    << cell_y << ','
                    << loads[i].actor_count << ','
                    << index.cells.size()
                    << '\n';
                if (++aoi_hotspot_pending_flush_count_ >= 4) {
                    aoi_hotspot_log_stream_.flush();
                    aoi_hotspot_pending_flush_count_ = 0;
                }
            }
        }
    }
}

bool ZoneRuntime::EnsureAoiLogFilesReady_()
{
    auto ready = true;
    if (enable_aoi_perf_file_log_) {
        ready = EnsureAoiLogFileReady_(aoi_perf_log_stream_, aoi_perf_log_path_, "timestamp_ms,map_key,actor_id,candidate_count,visible_before,visible_after,occupied_cells,neighbor_cells,visible_calc_time_us,diff_calc_time_us,spawn_build_time_us,despawn_build_time_us,move_build_time_us,total_move_time_us") && ready;
    }
    if (enable_aoi_summary_file_log_) {
        ready = EnsureAoiLogFileReady_(aoi_summary_log_stream_, aoi_summary_log_path_, "timestamp_ms,map_key,moves,avg_visible_calc_us,max_visible_calc_us,avg_diff_calc_us,max_diff_calc_us,avg_batch_build_us,max_batch_build_us,avg_total_move_us,p95_total_move_us,max_total_move_us,avg_candidate_count,max_candidate_count,avg_visible_after,max_visible_after,actor_count") && ready;
    }
    if (enable_aoi_hotspot_file_log_) {
        ready = EnsureAoiLogFileReady_(aoi_hotspot_log_stream_, aoi_hotspot_log_path_, "timestamp_ms,map_key,rank,cell_key,cell_x,cell_y,actor_count,occupied_cells") && ready;
    }
    return ready;
}

bool ZoneRuntime::EnsureAoiLogFileReady_(
    std::ofstream& stream,
    const std::filesystem::path& path,
    const char* header)
{
    if (path.empty()) {
        return false;
    }
    if (stream.is_open()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto needs_header = !std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) == 0;
    stream.open(path, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream.is_open()) {
        spdlog::warn("ZoneRuntime failed to open AOI log file. zone_server_id={} path={}", zone_server_id_, path.string());
        return false;
    }
    if (needs_header) {
        stream << header << '\n';
        stream.flush();
    }
    return true;
}

void ZoneRuntime::RotateAoiLogFileIfNeeded_(
    std::ofstream& stream,
    const std::filesystem::path& path,
    const char* header)
{
    constexpr std::uintmax_t kMaxLogBytes = 50ull * 1024ull * 1024ull;
    if (!stream.is_open()) {
        return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < kMaxLogBytes) {
        return;
    }
    stream.flush();
    stream.close();
    const auto rotated_path = path;
    auto backup_path = rotated_path;
    backup_path += ".1";
    std::filesystem::remove(backup_path, ec);
    ec.clear();
    std::filesystem::rename(rotated_path, backup_path, ec);
    if (ec) {
        spdlog::warn("ZoneRuntime AOI log rotation failed. zone_server_id={} path={} reason={}", zone_server_id_, path.string(), ec.message());
    }
    EnsureAoiLogFileReady_(stream, path, header);
}

std::uint64_t ZoneRuntime::CurrentUnixTimestampMs_()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::int32_t ZoneRuntime::DecodeSpatialCellX_(std::uint64_t cell_key) noexcept
{
    return static_cast<std::int32_t>(cell_key >> 32);
}

std::int32_t ZoneRuntime::DecodeSpatialCellY_(std::uint64_t cell_key) noexcept
{
    return static_cast<std::int32_t>(cell_key & 0xffffffffu);
}

void ZoneRuntime::ReapEmptyDungeonInstances_(std::chrono::steady_clock::time_point now)
{
    bool changed = false;
    for (auto it = map_instances_.begin(); it != map_instances_.end();) {
        const auto& inst = it->second;
        if (inst.dungeon_instance && inst.active_player_count == 0 && (now - inst.last_access_at) >= dc::k_ReapEmptyDungeonInstances_) {
            it = map_instances_.erase(it);
            changed = true;
            continue;
        }
        ++it;
    }
    if (changed) {
        RefreshMetrics_();
    }
}

void ZoneRuntime::RefreshMetrics_()
{
    std::uint32_t players = 0;
    for (const auto& [_, inst] : map_instances_) {
        players += inst.active_player_count;
    }
    active_map_instance_count_.store(static_cast<std::uint16_t>(map_instances_.size()), std::memory_order_relaxed);
    active_player_count_.store(static_cast<std::uint16_t>(players), std::memory_order_relaxed);
    load_score_.store(static_cast<std::uint16_t>(map_instances_.size() + players), std::memory_order_relaxed);
}

bool ZoneRuntime::ValidateZoneMapRuntimeData_(std::uint32_t map_template_id) const
{
    if (!OwnsMapTemplate_(map_template_id)) {
        spdlog::warn("ZoneRuntime map validation failed. logical_zone_id={} map_template_id={} reason=not_served_by_process",
            zone_id_, map_template_id);
        return false;
    }

    const auto gameplay_zone_id = ResolveGameplayZoneId_(map_template_id);
    if (!dc::zone::ZoneRuntimeDataStore::HasMap(gameplay_zone_id, map_template_id)) {
        spdlog::warn("ZoneRuntime map validation failed. logical_zone_id={} gameplay_zone_id={} map_template_id={} source={}",
            zone_id_, gameplay_zone_id, map_template_id, dc::zone::ZoneRuntimeDataStore::SnapshotStatus().source);
        return false;
    }
    return true;
}

bool ZoneRuntime::OwnsMapTemplate_(std::uint32_t map_template_id) const noexcept
{
    if (served_map_ids_.empty()) {
        return true;
    }
    return std::find(served_map_ids_.begin(), served_map_ids_.end(), map_template_id) != served_map_ids_.end();
}

std::uint16_t ZoneRuntime::ResolveGameplayZoneId_(std::uint32_t map_template_id) const noexcept
{
    return static_cast<std::uint16_t>(map_template_id == 0 ? zone_id_ : map_template_id);
}

void ZoneRuntime::LogZoneRegionSummary_(std::uint32_t map_template_id, std::int32_t x, std::int32_t y) const
{
    const auto gameplay_zone_id = ResolveGameplayZoneId_(map_template_id);
    const auto* safe = dc::zone::ZoneRuntimeDataStore::FindSafeRegion(gameplay_zone_id, map_template_id, x, y);
    const auto* special = dc::zone::ZoneRuntimeDataStore::FindSpecialRegion(gameplay_zone_id, map_template_id, x, y);
    const auto npc_count = dc::zone::ZoneRuntimeDataStore::GetNpcRegions(gameplay_zone_id, map_template_id).size();
    const auto monster_count = dc::zone::ZoneRuntimeDataStore::GetMonsterRegions(gameplay_zone_id, map_template_id).size();
    spdlog::debug("ZoneRuntime region summary. logical_zone_id={} gameplay_zone_id={} map_template_id={} pos=({}, {}) safe={} special={} npc_regions={} monster_regions={}",
        zone_id_, gameplay_zone_id, map_template_id, x, y, safe != nullptr ? 1 : 0, special != nullptr ? 1 : 0, npc_count, monster_count);
}

std::uint64_t ZoneRuntime::MakeMapInstanceKey_(std::uint32_t map_template_id, std::uint32_t instance_id) noexcept
{
    return (static_cast<std::uint64_t>(map_template_id) << 32) | static_cast<std::uint64_t>(instance_id);
}

bool ZoneRuntime::IsDungeonMapTemplate_(std::uint32_t map_template_id) noexcept
{
    return map_template_id >= 2000;
}

} // namespace svr
