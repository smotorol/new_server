#include "channel_handler.h"

#include <algorithm>
#include <iterator>
#include <memory>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "services/channel/runtime/channel_runtime.h"
#include "services/channel/actors/channel_actors.h"

bool ChannelHandler::HandleWorldMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_move>(body, body_len);
	if (!req) return false;

	const std::uint64_t char_id = GetActorIdBySession(sid);
	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	const std::uint32_t zone_id = a.zone_id;
	a.pos = { req->x, req->y };

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial == 0) return true;
	auto self = shared_from_this();

	svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [self, dwProID, sid, serial, zone_id, char_id, nx = req->x, ny = req->y]() {
		auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
		auto diff = z.Move(char_id, { nx, ny }, sid, serial);

		std::vector<std::uint64_t> entered;
		std::vector<std::uint64_t> exited;
		{
			auto oldv = diff.old_vis;
			auto newv = diff.new_vis;
			std::sort(oldv.begin(), oldv.end());
			std::sort(newv.begin(), newv.end());
			entered.reserve(newv.size());
			exited.reserve(oldv.size());
			std::set_difference(newv.begin(), newv.end(), oldv.begin(), oldv.end(), std::back_inserter(entered));
			std::set_difference(oldv.begin(), oldv.end(), newv.begin(), newv.end(), std::back_inserter(exited));
		}

		for (auto oid : entered) {
			auto itp = z.players.find(oid);
			if (itp == z.players.end()) continue;
			proto::S2C_player_spawn smsg{};
			smsg.char_id = oid;
			smsg.x = itp->second.pos.x;
			smsg.y = itp->second.pos.y;
			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_spawn, (std::uint16_t)sizeof(smsg));
			self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&smsg));
		}
		for (auto oid : exited) {
			proto::S2C_player_despawn dmsg{};
			dmsg.char_id = oid;
			auto h = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn, (std::uint16_t)sizeof(dmsg));
			self->Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&dmsg));
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
			auto it = z.players.find(rid);
			if (it == z.players.end()) continue;
			if (it->second.sid == 0 || it->second.serial == 0) continue;
			z.EnqueueSend_(it->second.sid, it->second.serial, h_spawn, body_self_spawn, (std::uint16_t)proto::S2CMsg::player_spawn);
		}
		for (auto rid : exited) {
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
			auto it = z.players.find(rid);
			if (it == z.players.end()) continue;
			if (it->second.sid == 0 || it->second.serial == 0) continue;
			z.EnqueueSend_(it->second.sid, it->second.serial, h_move, body_move, (std::uint16_t)proto::S2CMsg::player_move, char_id);
		}

		z.FlushPendingSends_(static_cast<ChannelHandler&>(*self), dwProID);

		if (z.HasPendingNet_()) {
			auto flusher = std::make_shared<std::function<void(int)>>();
			*flusher = [self, dwProID, zone_id, flusher](int depth) {
				auto& zf = svr::g_Main.GetOrCreateZoneActor(zone_id);
				zf.FlushPendingSends_(static_cast<ChannelHandler&>(*self), dwProID, 1500, 1 * 1024 * 1024);
				if (depth < 16 && zf.HasPendingNet_()) {
					svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [flusher, depth]() { (*flusher)(depth + 1); });
				}
			};
			svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [flusher]() { (*flusher)(0); });
		}
	});
	return true;
}

bool ChannelHandler::HandleWorldBenchMove(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_bench_move>(body, body_len);
	if (!req) return false;

	svr::g_c2s_bench_move_rx.fetch_add(1, std::memory_order_relaxed);

	const std::uint64_t char_id = GetActorIdBySession(sid);
	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	const std::uint32_t zone_id = a.zone_id;
	a.pos = { req->x, req->y };

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial == 0) return true;
	auto self = shared_from_this();
	auto* th = this;

	svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id),
		[self, th, dwProID, sid, zone_id, char_id,
		seq = req->seq, work_us = req->work_us, nx = req->x, ny = req->y, cts = req->client_ts_ns]() {
		const std::uint32_t serial = th->GetLatestSerial(sid);

		auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
		if (serial != 0) {
			z.MoveFastUpdate(char_id, { nx, ny }, sid, serial);
			z.EnqueueBenchAck(sid, serial, seq, cts, zone_id);
		}

		if (work_us > 0) {
			std::this_thread::sleep_for(std::chrono::microseconds(work_us));
		}

		z.FlushMoveTickIfDue_(static_cast<ChannelHandler&>(*self), dwProID, false);
	});
	return true;
}

bool ChannelHandler::HandleWorldBenchReset()
{
	svr::g_Main.RequestBenchReset();
	return true;
}

bool ChannelHandler::HandleWorldBenchMeasure(const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_bench_measure>(body, body_len);
	if (!req) return false;
	const int seconds = (int)std::max<proto::u32>(1, req->seconds);
	svr::g_Main.RequestBenchMeasure(seconds);
	return true;
}

bool ChannelHandler::HandleWorldSpawnMonster(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_spawn_monster>(body, body_len);
	if (!req) return false;

	const std::uint64_t char_id = GetActorIdBySession(sid);
	auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
	const std::uint32_t zone_id = a.zone_id;
	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial == 0) return true;

	auto self = shared_from_this();
	svr::g_Main.PostActor(svr::MakeZoneActorId(zone_id), [self, dwProID, sid, serial, zone_id, tid = req->template_id]() {
		auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);
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
