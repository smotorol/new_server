#include "services/zone/runtime/zone_runtime.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <inipp/inipp.h>

#include "server_common/config/runtime_config_path.h"
#include "server_common/data/zone_runtime_data.h"
#include "server_common/session/session_key.h"
#include "services/zone/handler/zone_world_handler.h"

namespace svr {
void ZoneRuntime::OnWorldRegisterAckFromHandler(std::uint32_t sid, std::uint32_t serial) { MarkWorldRegistered_(sid, serial); }
void ZoneRuntime::OnWorldDisconnectedFromHandler(std::uint32_t sid, std::uint32_t serial) { MarkWorldDisconnected_(sid, serial); }
void ZoneRuntime::OnMapAssignRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZoneMapAssignRequest& req) { OnMapAssignRequest(sid, serial, req); }
void ZoneRuntime::OnPlayerEnterRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerEnter& req) { OnPlayerEnterRequest_(sid, serial, req); }
void ZoneRuntime::OnPlayerLeaveRequestFromHandler(std::uint32_t sid, std::uint32_t serial, const pt_wz::WorldZonePlayerLeave& req) { OnPlayerLeaveRequest_(sid, serial, req); }

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

    spdlog::info(
        "ZoneRuntime INI loaded. file={} server_id={} logical_zone_id={} channel_id={} world_remote={}:{} served_maps={}",
        ini_path.string(),
        zone_server_id_,
        zone_id_,
        channel_id_,
        world_host_,
        world_port_,
        fmt::join(served_map_ids_, ","));
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
    player_bindings_[req.char_id] = PlayerBindingState{ key, req.map_template_id, req.instance_id };
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
    const auto key = bind_it->second.map_key;
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
