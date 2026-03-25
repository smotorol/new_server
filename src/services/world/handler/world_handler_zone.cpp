#include "services/world/handler/world_handler.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <thread>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "server_common/data/zone_runtime_data.h"
#include "server_common/session/session_key.h"
#include "services/world/actors/world_actors.h"
#include "services/world/common/aoi_broadcast_sanitize.h"
#include "services/world/metrics/world_metrics.h"

namespace {
    struct ZoneMapStateSnapshot {
        std::uint32_t zone_id = 0;
        std::uint32_t map_id = 0;
        std::uint32_t instance_id = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
    };

    ZoneMapStateSnapshot CaptureZoneMapState_(const svr::PlayerActor& actor) noexcept
    {
        const auto pos = actor.GetPosition();
        return ZoneMapStateSnapshot{
            actor.GetZoneId(),
            actor.GetMapId(),
            actor.GetMapInstanceId(),
            pos.x,
            pos.y,
        };
    }

    bool IsSameZoneMapState_(const ZoneMapStateSnapshot& lhs, const ZoneMapStateSnapshot& rhs) noexcept
    {
        return lhs.zone_id == rhs.zone_id &&
            lhs.map_id == rhs.map_id &&
            lhs.instance_id == rhs.instance_id &&
            lhs.x == rhs.x &&
            lhs.y == rhs.y;
    }
}

void WorldHandler::SendZoneMapState(
    std::uint32_t dwProID,
    std::uint32_t sid,
    std::uint32_t serial,
    std::uint64_t char_id,
    std::uint32_t zone_id,
    std::uint32_t map_id,
    std::int32_t x,
    std::int32_t y,
    proto::ZoneMapStateReason reason)
{
    proto::S2C_zone_map_state res{};
    res.char_id = char_id;
    res.zone_id = zone_id;
    res.map_id = map_id;
    res.x = x;
    res.y = y;
    res.reason = static_cast<proto::u16>(reason);
    auto h = proto::make_header(
        static_cast<std::uint16_t>(proto::S2CMsg::zone_map_state),
        static_cast<std::uint16_t>(sizeof(res)));
    Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));

    spdlog::debug(
        "zone_map_state sent. sid={} serial={} char_id={} zone_id={} map_id={} pos=({}, {}) reason={}",
        sid,
        serial,
        char_id,
        zone_id,
        map_id,
        x,
        y,
        static_cast<std::uint16_t>(reason));
}

bool WorldHandler::HandleWorldMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
    auto* req = proto::as<proto::C2S_move>(body, body_len);
    if (!req) return false;

    std::uint64_t char_id = 0;
    if (!ResolveAuthenticatedCharIdOrReject_("move", sid, char_id)) {
        return true;
    }
    auto& a = runtime().GetOrCreatePlayerActor(char_id);
    const auto before = CaptureZoneMapState_(a);
    const std::uint32_t serial = GetLatestSerial(sid);
    if (serial == 0) return true;

    if (before.x == req->x && before.y == req->y) {
        spdlog::debug(
            "move skipped identical position_update. sid={} serial={} char_id={} zone_id={} map_id={} pos=({}, {})",
            sid,
            serial,
            char_id,
            before.zone_id,
            before.map_id,
            before.x,
            before.y);
        return true;
    }

    a.SetWorldPosition(before.zone_id, before.map_id, before.instance_id, req->x, req->y);

    if (const auto* portal = dc::zone::ZoneRuntimeDataStore::FindTriggeredPortal(before.zone_id, before.map_id, req->x, req->y); portal != nullptr) {
        const auto dest_zone_id = (portal->dest_zone_id != 0) ? portal->dest_zone_id : before.zone_id;
        const auto dest_map_id = (portal->dest_map_id != 0) ? portal->dest_map_id : before.map_id;
        const auto dest_x = (portal->dest_x != 0) ? portal->dest_x : portal->center_x;
        const auto dest_y = (portal->dest_y != 0) ? portal->dest_y : portal->center_z;
        const bool zone_changed = (dest_zone_id != before.zone_id) || (dest_map_id != before.map_id);

        a.SetWorldPosition(dest_zone_id, dest_map_id, before.instance_id, dest_x, dest_y);
        const auto after = CaptureZoneMapState_(a);
        const auto self = shared_from_this();

        runtime().PostActor(svr::MakeZoneActorId(before.zone_id), [this, self, before, after, sid, serial, char_id]() {
            auto& old_zone = runtime().GetOrCreateZoneActor(before.zone_id);
            old_zone.Leave(char_id);
            if (after.zone_id == before.zone_id) {
                old_zone.JoinOrUpdate(char_id, { after.x, after.y }, sid, serial);
            }
        });

        if (after.zone_id != before.zone_id) {
            runtime().PostActor(svr::MakeZoneActorId(after.zone_id), [this, self, after, sid, serial, char_id]() {
                auto& new_zone = runtime().GetOrCreateZoneActor(after.zone_id);
                new_zone.JoinOrUpdate(char_id, { after.x, after.y }, sid, serial);
            });
        }

        SendZoneMapState(dwProID, sid, serial, char_id, after.zone_id, after.map_id, after.x, after.y, proto::ZoneMapStateReason::portal_moved);
        if (zone_changed) {
            SendZoneMapState(dwProID, sid, serial, char_id, after.zone_id, after.map_id, after.x, after.y, proto::ZoneMapStateReason::zone_changed);
        }

        spdlog::info(
            "portal transition committed. sid={} serial={} char_id={} old_zone={} old_map={} old_pos=({}, {}) new_zone={} new_map={} new_pos=({}, {}) portal_value02={}",
            sid,
            serial,
            char_id,
            before.zone_id,
            before.map_id,
            before.x,
            before.y,
            after.zone_id,
            after.map_id,
            after.x,
            after.y,
            portal->value02);
        return true;
    }

    const auto after = CaptureZoneMapState_(a);
    if (IsSameZoneMapState_(before, after)) {
        spdlog::debug(
            "move skipped duplicate zone_map_state. sid={} serial={} char_id={} zone_id={} map_id={} pos=({}, {})",
            sid,
            serial,
            char_id,
            after.zone_id,
            after.map_id,
            after.x,
            after.y);
        return true;
    }

    SendZoneMapState(dwProID, sid, serial, char_id, after.zone_id, after.map_id, after.x, after.y, proto::ZoneMapStateReason::position_update);
    auto self = shared_from_this();
    const std::uint32_t zone_id = after.zone_id;

    runtime().PostActor(svr::MakeZoneActorId(zone_id), [this, self, dwProID, sid, serial, zone_id, char_id, nx = after.x, ny = after.y]() {
        auto& z = runtime().GetOrCreateZoneActor(zone_id);
        auto diff = z.Move(char_id, { nx, ny }, sid, serial);

        std::vector<std::uint64_t> entered = std::move(diff.entered_vis);
        std::vector<std::uint64_t> exited = std::move(diff.exited_vis);
        if (entered.empty() && exited.empty() &&
            (!diff.old_vis.empty() || !diff.new_vis.empty())) {
            auto oldv = diff.old_vis;
            auto newv = diff.new_vis;
            std::sort(oldv.begin(), oldv.end());
            std::sort(newv.begin(), newv.end());
            entered.reserve(newv.size());
            exited.reserve(oldv.size());
            std::set_difference(newv.begin(), newv.end(), oldv.begin(), oldv.end(), std::back_inserter(entered));
            std::set_difference(oldv.begin(), oldv.end(), newv.begin(), newv.end(), std::back_inserter(exited));
        }

        svr::metrics::g_aoi_entered_entities.fetch_add(static_cast<std::uint64_t>(entered.size()), std::memory_order_relaxed);
        svr::metrics::g_aoi_exited_entities.fetch_add(static_cast<std::uint64_t>(exited.size()), std::memory_order_relaxed);
        svr::metrics::g_aoi_move_events.fetch_add(1, std::memory_order_relaxed);
        svr::metrics::g_aoi_move_fanout.fetch_add(static_cast<std::uint64_t>(diff.new_vis.size()), std::memory_order_relaxed);

        const auto sanitized_entered = svr::aoi::SanitizeEntityIds(entered);
        if (entered.size() > sanitized_entered.size()) {
            svr::metrics::g_aoi_sanitize_removed_entered.fetch_add(static_cast<std::uint64_t>(entered.size() - sanitized_entered.size()), std::memory_order_relaxed);
        }
        if (!sanitized_entered.empty()) {
            std::vector<proto::S2C_player_spawn_item> spawn_items;
            spawn_items.reserve(sanitized_entered.size());
            for (auto oid : sanitized_entered) {
                auto itp = z.players.find(oid);
                if (itp == z.players.end()) {
                    continue;
                }
                proto::S2C_player_spawn_item item{};
                item.char_id = oid;
                item.x = itp->second.pos.x;
                item.y = itp->second.pos.y;
                spawn_items.push_back(item);
            }

            if (!spawn_items.empty()) {
                const auto count = svr::aoi::ClampBatchEntityCount(spawn_items.size());
                spawn_items.resize(count);
                const std::size_t body_size = svr::aoi::SpawnBatchBodySize(count);
                std::vector<char> body(body_size);
                auto* pkt = reinterpret_cast<proto::S2C_player_spawn_batch*>(body.data());
                pkt->count = count;
                for (std::size_t i = 0; i < count; ++i) {
                    pkt->items[i] = spawn_items[i];
                }
                auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn_batch, (std::uint16_t)body_size);
                self->Send(dwProID, sid, serial, h, body.data());
            }
        }

        auto sanitized_exited = svr::aoi::SanitizeEntityIds(exited);
        if (exited.size() > sanitized_exited.size()) {
            svr::metrics::g_aoi_sanitize_removed_exited.fetch_add(static_cast<std::uint64_t>(exited.size() - sanitized_exited.size()), std::memory_order_relaxed);
        }

        if (!sanitized_exited.empty()) {
            const auto count = svr::aoi::ClampBatchEntityCount(sanitized_exited.size());
            sanitized_exited.resize(count);
            const std::size_t body_size = svr::aoi::DespawnBatchBodySize(count);
            std::vector<char> body(body_size);
            auto* pkt = reinterpret_cast<proto::S2C_player_despawn_batch*>(body.data());
            pkt->count = count;
            for (std::size_t i = 0; i < count; ++i) {
                pkt->items[i].char_id = sanitized_exited[i];
            }
            auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn_batch, (std::uint16_t)body_size);
            self->Send(dwProID, sid, serial, h, body.data());
        }

        proto::S2C_player_spawn self_spawn{};
        self_spawn.char_id = char_id;
        self_spawn.x = nx;
        self_spawn.y = ny;
        auto h_spawn = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn, (std::uint16_t)sizeof(self_spawn));
        auto body_self_spawn = svr::ZoneActor::MakeBody_(self_spawn);

        proto::S2C_player_despawn self_des{};
        self_des.char_id = char_id;
        auto h_des = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn, (std::uint16_t)sizeof(self_des));
        auto body_self_des = svr::ZoneActor::MakeBody_(self_des);

        for (auto rid : sanitized_entered) {
            auto it = z.players.find(rid);
            if (it == z.players.end()) continue;
            if (it->second.sid == 0 || it->second.serial == 0) continue;
            z.EnqueueSend_(it->second.sid, it->second.serial, h_spawn, body_self_spawn, (std::uint16_t)proto::S2CMsg::player_spawn);
        }
        for (auto rid : sanitized_exited) {
            auto it = z.players.find(rid);
            if (it == z.players.end()) continue;
            if (it->second.sid == 0 || it->second.serial == 0) continue;
            z.EnqueueSend_(it->second.sid, it->second.serial, h_des, body_self_des, (std::uint16_t)proto::S2CMsg::player_despawn);
        }

        proto::S2C_player_move mmsg{};
        mmsg.char_id = char_id;
        mmsg.x = nx;
        mmsg.y = ny;
        auto h_move = proto::make_header((std::uint16_t)proto::S2CMsg::player_move, (std::uint16_t)sizeof(mmsg));
        auto body_move = svr::ZoneActor::MakeBody_(mmsg);
        const auto sanitized_new_vis = svr::aoi::SanitizeEntityIds(diff.new_vis);
        if (diff.new_vis.size() > sanitized_new_vis.size()) {
            svr::metrics::g_aoi_sanitize_removed_new_vis.fetch_add(static_cast<std::uint64_t>(diff.new_vis.size() - sanitized_new_vis.size()), std::memory_order_relaxed);
        }

        for (auto rid : sanitized_new_vis) {
            auto it = z.players.find(rid);
            if (it == z.players.end()) continue;
            if (it->second.sid == 0 || it->second.serial == 0) continue;
            z.EnqueueSend_(it->second.sid, it->second.serial, h_move, body_move, (std::uint16_t)proto::S2CMsg::player_move, char_id);
        }

        z.FlushPendingSends_(static_cast<WorldHandler&>(*self), dwProID);

        if (z.HasPendingNet_()) {
            auto flusher = std::make_shared<std::function<void(int)>>();
            *flusher = [this, self, dwProID, zone_id, flusher](int depth) {
                auto& zf = runtime().GetOrCreateZoneActor(zone_id);
                zf.FlushPendingSends_(static_cast<WorldHandler&>(*self), dwProID, 1500, 1 * 1024 * 1024);
                if (depth < 16 && zf.HasPendingNet_()) {
                    runtime().PostActor(svr::MakeZoneActorId(zone_id), [flusher, depth]() { (*flusher)(depth + 1); });
                }
            };
            runtime().PostActor(svr::MakeZoneActorId(zone_id), [flusher]() { (*flusher)(0); });
        }
    });
    return true;
}

bool WorldHandler::HandleWorldBenchMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
    auto* req = proto::as<proto::C2S_bench_move>(body, body_len);
    if (!req) return false;

    svr::metrics::g_c2s_bench_move_rx.fetch_add(1, std::memory_order_relaxed);

    std::uint64_t char_id = GetActorIdBySession(sid);
    if (char_id == 0) {
        char_id = (1ull << 63) | static_cast<std::uint64_t>(sid);
    }
    auto& a = runtime().GetOrCreatePlayerActor(char_id);
    a.SetWorldPosition(a.GetZoneId(), a.GetMapId(), a.GetMapInstanceId(), req->x, req->y);

    const std::uint32_t serial = GetLatestSerial(sid);
    if (serial == 0) return true;

    SendZoneMapState(dwProID, sid, serial, char_id, a.GetZoneId(), a.GetMapId(), req->x, req->y, proto::ZoneMapStateReason::position_update);
    auto self = shared_from_this();
    auto* th = this;
    const std::uint32_t zone_id = a.GetZoneId();

    runtime().PostActor(svr::MakeZoneActorId(zone_id),
        [this, self, th, dwProID, sid, zone_id, char_id,
        seq = req->seq, work_us = req->work_us, nx = req->x, ny = req->y, cts = req->client_ts_ns]() {
        const std::uint32_t serial = th->GetLatestSerial(sid);

        auto& z = runtime().GetOrCreateZoneActor(zone_id);
        if (dc::IsValidSessionKey(sid, serial)) {
            z.MoveFastUpdate(char_id, { nx, ny }, sid, serial);
            z.EnqueueBenchAck(sid, serial, seq, cts, zone_id);
        }

        if (work_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(work_us));
        }

        z.FlushMoveTickIfDue_(static_cast<WorldHandler&>(*self), dwProID, false);
    });
    return true;
}

bool WorldHandler::HandleWorldBenchReset()
{
    runtime().RequestBenchReset();
    return true;
}

bool WorldHandler::HandleWorldBenchMeasure(const char* body, std::size_t body_len)
{
    auto* req = proto::as<proto::C2S_bench_measure>(body, body_len);
    if (!req) return false;
    const int seconds = (int)std::max<proto::u32>(1, req->seconds);
    runtime().RequestBenchMeasure(seconds);
    return true;
}

bool WorldHandler::HandleWorldSpawnMonster(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
    auto* req = proto::as<proto::C2S_spawn_monster>(body, body_len);
    if (!req) return false;

    std::uint64_t char_id = 0;
    if (!ResolveAuthenticatedCharIdOrReject_("spawn_monster", sid, char_id)) {
        return true;
    }
    auto& a = runtime().GetOrCreatePlayerActor(char_id);
    const std::uint32_t zone_id = a.GetZoneId();
    const std::uint32_t serial = GetLatestSerial(sid);
    if (serial == 0) return true;

    auto self = shared_from_this();
    runtime().PostActor(svr::MakeZoneActorId(zone_id), [this, self, dwProID, sid, serial, zone_id, tid = req->template_id]() {
        auto& z = runtime().GetOrCreateZoneActor(zone_id);
        svr::MonsterState m{};
        m.id = z.next_monster_id++;
        if (tid == 1) { m.hp = 120; m.atk = 15; m.def = 4; m.drop_item_id = 2001; m.drop_count = 1; }
        else { m.hp = 50; m.atk = 8; m.def = 1; m.drop_item_id = 1001; m.drop_count = 1; }
        z.monsters[m.id] = m;

        proto::S2C_spawn_monster_ok res{};
        res.monster_id = m.id;
        res.hp = m.hp;
        res.atk = m.atk;
        res.def = m.def;
        auto h = proto::make_header((std::uint16_t)proto::S2CMsg::spawn_monster_ok, (std::uint16_t)sizeof(res));
        self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
    });
    return true;
}
