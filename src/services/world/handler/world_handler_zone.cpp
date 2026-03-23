#include "services/world/handler/world_handler.h"

#include <algorithm>
#include <iterator>
#include <memory>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "server_common/session/session_key.h"
#include "services/world/actors/world_actors.h"
#include "services/world/metrics/world_metrics.h"
#include "server_common/session/session_key.h"

bool WorldHandler::HandleWorldMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_move>(body, body_len);
	if (!req) return false;

	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("move", sid, char_id)) {
		return true;
	}
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	const std::uint32_t zone_id = a.zone_id;
	a.pos = { req->x, req->y };

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial == 0) return true;
	auto self = shared_from_this();

	runtime().PostActor(svr::MakeZoneActorId(zone_id), [this, self, dwProID, sid, serial, zone_id, char_id, nx = req->x, ny = req->y]() {
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

		svr::metrics::g_aoi_entered_entities.fetch_add(
			static_cast<std::uint64_t>(entered.size()),
			std::memory_order_relaxed);
		svr::metrics::g_aoi_exited_entities.fetch_add(
			static_cast<std::uint64_t>(exited.size()),
			std::memory_order_relaxed);
		svr::metrics::g_aoi_move_events.fetch_add(1, std::memory_order_relaxed);
		svr::metrics::g_aoi_move_fanout.fetch_add(
			static_cast<std::uint64_t>(diff.new_vis.size()),
			std::memory_order_relaxed);

		if (!entered.empty()) {
			std::vector<proto::S2C_player_spawn_item> spawn_items;
			spawn_items.reserve(entered.size());
			for (auto oid : entered) {
				if (oid == 0) {
					continue;
				}
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
				const auto count = static_cast<std::uint16_t>(spawn_items.size());
				const std::size_t body_size = sizeof(proto::S2C_player_spawn_batch) +
					((std::size_t)count - 1) * sizeof(proto::S2C_player_spawn_item);
				std::vector<char> body(body_size);
				auto* pkt = reinterpret_cast<proto::S2C_player_spawn_batch*>(body.data());
				pkt->count = count;
				for (std::size_t i = 0; i < spawn_items.size(); ++i) {
					pkt->items[i] = spawn_items[i];
				}
				auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn_batch, (std::uint16_t)body_size);
				self->Send(dwProID, sid, serial, h, body.data());
			}
		}

		std::vector<std::uint64_t> sanitized_exited;
		if (!exited.empty()) {
			sanitized_exited.reserve(exited.size());
			for (auto rid : exited) {
				if (rid == 0) {
					continue;
				}
				sanitized_exited.push_back(rid);
			}
		}

		if (!sanitized_exited.empty()) {
			const auto count = static_cast<std::uint16_t>(sanitized_exited.size());
			const std::size_t body_size = sizeof(proto::S2C_player_despawn_batch) +
				((std::size_t)count - 1) * sizeof(proto::S2C_player_despawn_item);
			std::vector<char> body(body_size);
			auto* pkt = reinterpret_cast<proto::S2C_player_despawn_batch*>(body.data());
			pkt->count = count;
			for (std::size_t i = 0; i < sanitized_exited.size(); ++i) {
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

		for (auto rid : entered) {
			if (rid == 0) continue;
			auto it = z.players.find(rid);
			if (it == z.players.end()) continue;
			if (it->second.sid == 0 || it->second.serial == 0) continue;
			z.EnqueueSend_(it->second.sid, it->second.serial, h_spawn, body_self_spawn, (std::uint16_t)proto::S2CMsg::player_spawn);
		}
		for (auto rid : sanitized_exited) {
			if (rid == 0) continue;
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

		for (auto rid : diff.new_vis) {
			if (rid == 0) continue;
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

	std::uint64_t char_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("bench_move", sid, char_id)) {
		return true;
	}
	auto& a = runtime().GetOrCreatePlayerActor(char_id);
	const std::uint32_t zone_id = a.zone_id;
	a.pos = { req->x, req->y };

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial == 0) return true;
	auto self = shared_from_this();
	auto* th = this;

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
	const std::uint32_t zone_id = a.zone_id;
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
