#include "channel_handler.h"
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <thread>
#include <functional>
#include <memory>
#include "core/util/string_utils.h"
#include "net/actor/actor_system.h"
#include "proto/common/packet_util.h"
#include "proto/common/proto_base.h"
#include "services/channel/runtime/channel_runtime.h"
#include "services/channel/actors/channel_actors.h"
#include "db/core/dqs_payloads.h"


// ===== 샘플 캐릭터 상태(바이너리 blob) =====
#pragma pack(push, 1)
struct DemoCharState
{
	std::uint64_t char_id = 0;
	std::uint32_t gold = 0;
	std::uint32_t version = 0;
};
#pragma pack(pop)
static_assert(sizeof(DemoCharState) == 16);

static std::string SerializeDemo(const DemoCharState& s)
{
	return std::string(reinterpret_cast<const char*>(&s), sizeof(s));
}
static bool TryDeserializeDemo(const std::string& blob, DemoCharState& out)
{
	if (blob.size() != sizeof(DemoCharState)) return false;
	std::memcpy(&out, blob.data(), sizeof(DemoCharState));
	return true;
}

std::uint64_t ChannelHandler::GetActorIdBySession(std::uint32_t sid) const
{
	if (const auto char_id = svr::g_Main.FindCharIdBySession(sid); char_id != 0)
		return char_id;
	return static_cast<std::uint64_t>(sid);
}

std::uint64_t ChannelHandler::ResolveActorId(std::uint32_t session_idx) const
{
	// World 라인은 (sid -> char_id) 바인딩 이후 char_id Actor로 라우팅
	return GetActorIdBySession(session_idx);
}

std::uint64_t ChannelHandler::ResolveActorIdForPacket(std::uint32_t session_idx,
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
bool ChannelHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	if (!pMsgHeader) return false;

	switch (dwProID)
	{
	case eLine_World:
		{
			return WorldLineAnalysis(dwProID, dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	case eLine_Login:
		{
			return LoginLineAnalysis(dwProID, dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	case eLine_Control:
		{
			return ControlLineAnalysis(dwProID, dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	}
	return false;
}

void ChannelHandler::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("ChannelHandler::AcceptClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);

	// ===== 샘플 플로우는 World 라인에서만 시연 =====
	if (dwProID != eLine_World) return;

	const std::uint32_t world_code = 0;

	// (샘플) 임시 char_id: 실제 서비스에서는 로그인 이후 확정된 char_id를 바인딩해야 함
	const std::uint64_t char_id = (std::uint64_t(dwIndex) << 32) | std::uint64_t(dwSerial);
	svr::g_Main.BindSessionCharId(dwIndex, char_id);
	// 1) Redis에서 상태 로드 시도
	DemoCharState st{};
	st.char_id = char_id;
	st.gold = 1000;   // 신규 기본값
	st.version = 1;

	if (auto blob = svr::g_Main.TryLoadCharacterState(world_code, char_id))
	{
		DemoCharState loaded{};
		if (TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
		{
			st = loaded;
			spdlog::info("[Demo] loaded from redis char_id={} gold={} ver={}", st.char_id, st.gold, st.version);
		}
		else
		{
			spdlog::warn("[Demo] redis blob invalid, recreate char_id={}", char_id);
		}
	}
	else
	{
		spdlog::info("[Demo] no redis state, create new char_id={}", char_id);
	}

	// 2) 샘플 게임 로직: 접속 보상 gold +10
	st.gold += 10;
	st.version += 1;
	const std::string out_blob = SerializeDemo(st);

	// ✅ 케이스1(Actor당 인스턴스) : PlayerActor 생성/초기화 + 세션 바인딩
	// - 이 람다는 char_id Actor shard에서 실행되므로, PlayerActor 상태를 락 없이 안전하게 만질 수 있다.
	svr::g_Main.PostActor(char_id, [char_id, sid = dwIndex, serial = dwSerial]() {
		auto& a = svr::g_Main.GetOrCreatePlayerActor(char_id);
		a.bind_session(sid, serial);
		// 기본 전투 스탯(샘플)
		a.combat.hp = 100;
		a.combat.max_hp = 100;
		a.combat.atk = 20;
		a.combat.def = 3;
		a.combat.gold = 1000;
		// zone init
		a.zone_id = 1;
		a.pos = { 0,0 };
		svr::g_Main.PostActor(svr::MakeZoneActorId(a.zone_id), [char_id, sid, serial]() {
			auto& z = svr::g_Main.GetOrCreateZoneActor(1);
			z.JoinOrUpdate(char_id, { 0,0 }, sid, serial);
		});
	});

	// 3) Redis 저장 + dirty 마킹 (기존 함수 사용!)
	svr::g_Main.CacheCharacterState(world_code, char_id, out_blob);

	// ✅ 클라에게 "바인딩된 ActorId(=char_id)" 통지 (테스트/벤치용)
	{
		proto::S2C_actor_bound res{};
		res.actor_id = char_id;
		auto h = proto::make_header((std::uint16_t)proto::S2CMsg::actor_bound,
			(std::uint16_t)sizeof(res));
		Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&res));
	}
}

void ChannelHandler::CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	// ✅ 늦게 도착한 disconnect(세션 재사용/재접속) 방지
	// - 최신 serial이 아니면 이미 다른 세션이 같은 index를 쓰고 있는 것
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug("CloseClientCheck ignored (stale). pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
		return;
	}

	// ===== 샘플: 로그아웃 시 즉시 DB 저장(flush_one_char) =====
	if (dwProID == eLine_World)
	{
		const std::uint32_t world_code = 0;
		std::uint64_t char_id = 0;

		char_id = svr::g_Main.UnbindSessionCharId(dwIndex);

		if (char_id != 0) {
			// ✅ 중요:
			// HandleDisconnected_ -> CloseClientCheck 는 io 스레드에서 호출될 수 있다.
			// ZoneActor 내부 컨테이너(players/cells)에 여기서 직접 접근하면 data race로 크래시가 난다.
			// => 반드시 ActorSystem(PostActor)로 넘겨서, 각 Actor 소유 샤드에서만 상태를 바꾼다.

			auto self = shared_from_this(); // shared_ptr<dc::NetworkEXBase>

			proto::S2C_player_despawn despawn{};
			despawn.char_id = char_id;
			auto despawn_h = proto::make_header((std::uint16_t)proto::S2CMsg::player_despawn,
				(std::uint16_t)sizeof(despawn));

			svr::g_Main.PostActor(char_id, [self, dwProID, world_code, char_id, despawn, despawn_h]() mutable {
				// 1) PlayerActor: 오프라인 마킹 + zone_id 확보
				auto& p = svr::g_Main.GetOrCreatePlayerActor(char_id);
				const std::uint32_t zone_id = p.zone_id;
				p.online = false;
				p.sid = 0;
				p.serial = 0;

				// 2) ZoneActor: Leave + neighbors 수집
				const std::uint64_t zid = svr::MakeZoneActorId(zone_id);
				svr::g_Main.PostActor(zid, [self, dwProID, zone_id, char_id, despawn, despawn_h]() mutable {
					auto& z = svr::g_Main.GetOrCreateZoneActor(zone_id);

					// 중복 disconnect 등 멱등 처리
					auto itp = z.players.find(char_id);
					if (itp == z.players.end()) {
						z.Leave(char_id);
						return;
					}

					const auto neigh = z.GatherNeighborsVec(itp->second.cx, itp->second.cy);
					z.Leave(char_id);

					// 3) 수신자 Actor에서 세션 체크 후 send
					for (auto other_id : neigh) {
						if (other_id == char_id) continue;
						auto it = z.players.find(other_id);
						if (it == z.players.end()) continue;
						if (it->second.sid == 0 || it->second.serial == 0) continue;
						self->Send(dwProID, it->second.sid, it->second.serial, despawn_h, reinterpret_cast<const char*>(&despawn));
					}
				});

				// 4) Actor 제거 + DB flush (PlayerActor 샤드)
				svr::g_Main.EraseActor(char_id);
				svr::g_Main.RequestFlushCharacter(world_code, char_id);
			});
		}

	}
	else {
		//std::lock_guard<std::mutex> lk(state_mtx_);
	}

	spdlog::info("ChannelHandler::CloseClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

bool ChannelHandler::LoginLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 LoginLineAnalysis 이식
	return false;
}

bool ChannelHandler::WorldLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	const std::uint16_t type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = pMsgHeader->m_wSize - MSG_HEADER_SIZE;

	switch (type)
	{
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

bool ChannelHandler::ControlLineAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 ControlLineAnalysis 이식
	return false;
}
