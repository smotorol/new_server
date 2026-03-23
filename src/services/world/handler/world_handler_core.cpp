#include "services/world/handler/world_handler.h"

#include <iostream>

#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "proto/client/world_proto.h"
#include "services/world/metrics/world_metrics.h"

namespace pt_w = proto::world;

std::uint64_t WorldHandler::GetActorIdBySession(std::uint32_t sid) const
{
	return runtime().FindCharIdBySession(sid);
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

	return default_actor;
}

bool WorldHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER * pMsgHeader, char* pMsg)
{
	const std::uint16_t type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = pMsgHeader->m_wSize - MSG_HEADER_SIZE;

	switch (type)
	{
	case static_cast<std::uint16_t>(pt_w::WorldC2SMsg::enter_world_with_token):
		return HandleEnterWorldWithToken(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::open_world_notice:
		return HandleWorldOpenWorldNotice(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::add_gold:
		return HandleWorldAddGold(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::get_stats:
		return HandleWorldGetStats(dwProID, n);
	case proto::C2SMsg::heal_self:
		return HandleWorldHealSelf(dwProID, n, pMsg, body_len);
	case proto::C2SMsg::move:
		return HandleWorldMove(dwProID, n, pMsg, body_len);
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
