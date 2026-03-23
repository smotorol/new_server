#include "services/world/handler/world_handler.h"

#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "server_common/session/session_key.h"

bool WorldHandler::HandleWorldActorSeqTest(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_actor_seq_test>(body, body_len);
	if (!req) return false;

	std::uint64_t actor_id = 0;
	if (!ResolveAuthenticatedCharIdOrReject_("actor_seq_test", sid, actor_id)) {
		return true;
	}

	thread_local std::unordered_map<std::uint64_t, std::uint32_t> last_seq;
	thread_local std::unordered_map<std::uint64_t, bool> in_progress;
	thread_local std::uint32_t error_count = 0;

	bool& busy = in_progress[actor_id];
	if (busy) {
		++error_count;
	}
	busy = true;

	const std::uint32_t prev = last_seq[actor_id];
	if (req->seq != prev + 1) {
		++error_count;
	}
	last_seq[actor_id] = req->seq;

	if (req->work_us > 0) {
		std::this_thread::sleep_for(std::chrono::microseconds(req->work_us));
	}

	busy = false;

	proto::S2C_actor_seq_ack res{};
	res.ok = 1;
	res.seq = req->seq;
	res.shard = (proto::u32)std::max(0, net::ActorSystem::current_shard_index());
	res.errors = error_count;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::actor_seq_ack,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}

bool WorldHandler::HandleWorldActorForward(std::uint32_t dwProID, std::uint32_t sid, const char* body, std::size_t body_len)
{
	auto* req = proto::as<proto::C2S_actor_forward>(body, body_len);
	if (!req) return false;

	std::uint64_t actor_id = req->target_actor_id;
	if (actor_id == 0) {
		if (!ResolveAuthenticatedCharIdOrReject_("actor_forward", sid, actor_id)) {
			return true;
		}
	}

	thread_local std::unordered_map<std::uint64_t, bool> in_progress;
	thread_local std::uint32_t error_count = 0;

	bool& busy = in_progress[actor_id];
	if (busy) { ++error_count; }
	busy = true;

	if (req->work_us > 0) {
		std::this_thread::sleep_for(std::chrono::microseconds(req->work_us));
	}

	busy = false;

	proto::S2C_actor_seq_ack res{};
	res.ok = 1;
	res.seq = req->tag;
	res.shard = (proto::u32)std::max(0, net::ActorSystem::current_shard_index());
	res.errors = error_count;

	auto h = proto::make_header((std::uint16_t)proto::S2CMsg::actor_seq_ack,
		(std::uint16_t)sizeof(res));

	const std::uint32_t serial = GetLatestSerial(sid);
	if (serial != 0) {
		Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&res));
	}
	return true;
}
