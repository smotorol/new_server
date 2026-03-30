#include "services/world/handler/world_handler.h"

#include <iostream>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/common/protobuf_packet_codec.h"
#include "proto/client/world_proto.h"
#include "services/world/runtime/world_runtime.h"
#include "services/world/metrics/world_metrics.h"

namespace pt_w = proto::world;

#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
#include "proto/generated/cpp/client_world.pb.h"
#define DC_WORLD_CLIENT_PROTOBUF 1
#else
#define DC_WORLD_CLIENT_PROTOBUF 0
#endif

std::uint64_t WorldHandler::GetActorIdBySession(std::uint32_t sid) const
{
	return runtime().FindCharIdBySession(sid);
}

void WorldHandler::SetSessionProtoMode(std::uint32_t sid, std::uint32_t serial, bool use_protobuf)
{
	std::lock_guard lk(session_proto_mode_mtx_);
	session_proto_mode_[sid] = { serial, use_protobuf };
}

bool WorldHandler::IsSessionProtoMode(std::uint32_t sid, std::uint32_t serial) const
{
	std::lock_guard lk(session_proto_mode_mtx_);
	const auto it = session_proto_mode_.find(sid);
	return it != session_proto_mode_.end() && it->second.first == serial && it->second.second;
}

void WorldHandler::ClearSessionProtoMode(std::uint32_t sid, std::uint32_t serial)
{
	std::lock_guard lk(session_proto_mode_mtx_);
	const auto it = session_proto_mode_.find(sid);
	if (it != session_proto_mode_.end() && it->second.first == serial) {
		session_proto_mode_.erase(it);
	}
}

bool WorldHandler::ResolveAuthenticatedCharIdOrReject_(
	const char* op_name,
	std::uint32_t sid,
	std::uint64_t& out_char_id) const
{
	out_char_id = GetActorIdBySession(sid);
	if (out_char_id != 0) {
		return true;
	}

	svr::metrics::g_world_unauth_packet_rejects.fetch_add(1, std::memory_order_relaxed);
	svr::metrics::g_world_unauth_last_sid.store(sid, std::memory_order_relaxed);
	spdlog::warn(
		"[auth] rejected unauthenticated world packet. op={} sid={}",
		(op_name ? op_name : "unknown"),
		sid);
	return false;
}

std::uint64_t WorldHandler::ResolveActorId(std::uint32_t session_idx) const
{
	// World 라인은 (sid -> char_id) 바인딩 이후 char_id Actor로 라우팅
	return GetActorIdBySession(session_idx);
}

std::uint64_t WorldHandler::ResolveActorIdForPacket(std::uint32_t session_idx,
	const _MSG_HEADER& header, const char* body, std::size_t body_len,
	std::uint64_t default_actor) const
{
	(void)session_idx;
	const std::uint16_t type = proto::get_type_u16(header);
	if (type == (std::uint16_t)proto::C2SMsg::actor_forward) {
		auto* req = proto::as<proto::C2S_actor_forward>(body, body_len);
		if (req && req->target_actor_id != 0) return req->target_actor_id;
	}

	// PvP는 "맞는 쪽(target_char_id)" Actor로 라우팅해서 타겟 HP를 단일 실행으로 갱신
	if (type == (std::uint16_t)proto::C2SMsg::attack_player) {
		auto* req = proto::as<proto::C2S_attack_player>(body, body_len);
		if (req && req->target_char_id != 0) return req->target_char_id;
	}

	// bench_move는 테스트용 명령으로, 인증 바인딩이 없어도 sid 단위 Actor로 분산 처리한다.
	// (default_actor가 0이면 shard hot-spot이 생기므로 session_idx를 fallback key로 사용)
	if (type == (std::uint16_t)proto::C2SMsg::bench_move && default_actor == 0) {
		return static_cast<std::uint64_t>(session_idx);
	}

	return default_actor;
}

bool WorldHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER * pMsgHeader, char* pMsg)
{
	const std::uint16_t type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = pMsgHeader->m_wSize - MSG_HEADER_SIZE;

	switch (type)
	{
	case static_cast<std::uint16_t>(pt_w::WorldC2SMsg::enter_world_with_token):
	{
#if DC_WORLD_CLIENT_PROTOBUF
		dc::proto::client::world::EnterWorldWithTokenRequest req;
		if (req.ParseFromArray(pMsg, static_cast<int>(body_len))) {
			SetSessionProtoMode(n, GetLatestSerial(n), true);
			return HandleEnterWorldWithToken(dwProID, n, pMsg, body_len, true);
		}
#endif
		return HandleEnterWorldWithToken(dwProID, n, pMsg, body_len, false);
	}
	case proto::C2SMsg::open_world_notice:
		return HandleWorldOpenWorldNotice(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::add_gold:
		return HandleWorldAddGold(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::get_stats:
	{
#if DC_WORLD_CLIENT_PROTOBUF
		dc::proto::client::world::GetStatsRequest req;
		if (req.ParseFromArray(pMsg, static_cast<int>(body_len))) {
			SetSessionProtoMode(n, GetLatestSerial(n), true);
			return HandleWorldGetStats(dwProID, n, true);
		}
#endif
		return HandleWorldGetStats(dwProID, n, false);
	}
	case proto::C2SMsg::heal_self:
		return HandleWorldHealSelf(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::move:
	{
#if DC_WORLD_CLIENT_PROTOBUF
		dc::proto::client::world::MoveRequest req;
		if (req.ParseFromArray(pMsg, static_cast<int>(body_len))) {
			SetSessionProtoMode(n, GetLatestSerial(n), true);
			return HandleWorldMove(dwProID, n, pMsg, body_len, true);
		}
#endif
		return HandleWorldMove(dwProID, n, pMsg, body_len, false);
	}
	case proto::C2SMsg::bench_move:
		return HandleWorldBenchMove(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::bench_reset:
		return HandleWorldBenchReset();
	case proto::C2SMsg::bench_measure:
		return HandleWorldBenchMeasure(pMsg, body_len);
	case proto::C2SMsg::spawn_monster:
		return HandleWorldSpawnMonster(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::attack_monster:
		return HandleWorldAttackMonster(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::attack_player:
		return HandleWorldAttackPlayer(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::actor_seq_test:
		return HandleWorldActorSeqTest(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::actor_forward:
		return HandleWorldActorForward(dwProID, n, pMsg, body_len);
	default:
		std::cout << "[World] unknown type=" << type << "\n";
		return true;
	}
}
