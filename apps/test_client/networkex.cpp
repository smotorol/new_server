#include "networkex.h"
#include <iostream>
#include <cstring>
#include "proto/common/proto_base.h"
#include "proto/client/login_proto.h"
#include "proto/client/world_proto.h"
#include "core/util/string_utils.h"
#include "proto/common/packet_util.h"
#include "net/tcp/tcp_client.h"
#include "net/tcp/tcp_session.h"
#include "define.h"
#include "GlobalClients.h"

namespace pt_login = proto::login;
namespace pt_world = proto::world;

std::atomic<bool> CNetworkEX::bench_quiet_{ false };
std::atomic<std::uint64_t> CNetworkEX::s_fallback_seq_{ 1 };

CNetworkEX::CNetworkEX(std::uint32_t pro_id)
	: dc::NetworkEXBase(pro_id)
{
	// ✅ handler(클라이언트 인스턴스) 단위로 shard 분산을 위한 고유 ActorId
	// - 0은 특별값이니 피함
	fallback_actor_id_ = s_fallback_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::uint64_t CNetworkEX::ResolveActorId(std::uint32_t /*session_idx*/) const
{
	// ready(=actor_bound 수신) 이후에는 서버가 내려준 char_id 기반 actor_id_ 사용
	const auto bound = actor_id_.load(std::memory_order_relaxed);
	return (bound != 0) ? bound : fallback_actor_id_;
}

void CNetworkEX::SetBenchQuiet(bool on) noexcept
{
	bench_quiet_.store(on, std::memory_order_relaxed);
}

void CNetworkEX::BenchReset() noexcept
{
	recv_move_pkts_.store(0, std::memory_order_relaxed);
	recv_move_items_.store(0, std::memory_order_relaxed);
	recv_spawn_.store(0, std::memory_order_relaxed);
	recv_despawn_.store(0, std::memory_order_relaxed);
	recv_ack_.store(0, std::memory_order_relaxed);
	rtt_sum_ns_.store(0, std::memory_order_relaxed);
	rtt_min_ns_.store(0, std::memory_order_relaxed);
	rtt_max_ns_.store(0, std::memory_order_relaxed);
}

CNetworkEX::BenchSnapshot CNetworkEX::BenchGetSnapshot() const noexcept
{
	BenchSnapshot s;
	s.recv_move_pkts = recv_move_pkts_.load(std::memory_order_relaxed);
	s.recv_move_items = recv_move_items_.load(std::memory_order_relaxed);
	s.recv_spawn = recv_spawn_.load(std::memory_order_relaxed);
	s.recv_despawn = recv_despawn_.load(std::memory_order_relaxed);
	s.recv_ack = recv_ack_.load(std::memory_order_relaxed);

	const auto ack = s.recv_ack;
	const long long sum = rtt_sum_ns_.load(std::memory_order_relaxed);
	const long long mn = rtt_min_ns_.load(std::memory_order_relaxed);
	const long long mx = rtt_max_ns_.load(std::memory_order_relaxed);
	if (ack > 0) {
		s.rtt_avg_ms = (double)sum / (double)ack / 1'000'000.0;
		s.rtt_min_ms = (double)mn / 1'000'000.0;
		s.rtt_max_ms = (double)mx / 1'000'000.0;
	}
	return s;
}

void CNetworkEX::wait_ready()
{
	if (is_ready()) return;
	std::unique_lock lk(ready_mtx_);
	ready_cv_.wait(lk, [&] { return ready_.load(std::memory_order_relaxed); });
}

bool CNetworkEX::has_login_result() const
{
	std::lock_guard lk(login_result_mtx_);
	return has_login_result_;
}

CNetworkEX::LoginResultState CNetworkEX::login_result() const
{
	std::lock_guard lk(login_result_mtx_);
	return login_result_;
}

void CNetworkEX::clear_login_result()
{
	std::lock_guard lk(login_result_mtx_);
	login_result_ = LoginResultState{};
	has_login_result_ = false;
}

bool CNetworkEX::DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	switch (static_cast<eLine>(dwProID))
	{
	case eLine::login_server:
	case eLine::world_server:
		{
			return LineAnalysis(dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	}
	return false;
}

void CNetworkEX::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	switch (static_cast<eLine>(dwProID))
	{
	case eLine::login_server:
		std::cout << "[Login] connected sid=" << dwIndex << "\n";
		break;
	case eLine::world_server:
		std::cout << "[World] connected sid=" << dwIndex << "\n";
		break;
	default:
		std::cout << "[Client] connected sid=" << dwIndex << "\n";
		break;
	}

	spdlog::info("CNetworkEX::AcceptClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

void CNetworkEX::CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	spdlog::info("CNetworkEX::CloseClientCheck pro={} index={} serial={}", dwProID, dwIndex, dwSerial);
}

bool CNetworkEX::LineAnalysis(std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	const std::uint16_t type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len = pMsgHeader->m_wSize - MSG_HEADER_SIZE;
	const auto line = static_cast<eLine>(pro_id());

	if (line == eLine::login_server)
	{
		switch (static_cast<pt_login::LoginS2CMsg>(type))
		{
		case pt_login::LoginS2CMsg::login_result:
			{
				auto* res = proto::as<pt_login::S2C_login_result>(pMsg, body_len);
				if (!res) return false;

				LoginResultState st{};
				st.ok = (res->ok != 0);
				st.account_id = res->account_id;
				st.char_id = res->char_id;
				st.world_port = res->world_port;
				st.world_host = res->world_host;
				st.login_session = res->login_session;
				st.world_token = res->world_token;

				{
					std::lock_guard lk(login_result_mtx_);
					login_result_ = std::move(st);
					has_login_result_ = true;
				}

				std::cout
					<< "[Login] login_result sid=" << n
					<< " ok=" << static_cast<int>(res->ok)
					<< " account_id=" << res->account_id
					<< " char_id=" << res->char_id
					<< " world_host=" << res->world_host
					<< " world_port=" << res->world_port
					<< " login_session=" << res->login_session
					<< " world_token=" << res->world_token
					<< "\n";
			}
			return true;
		default:
			std::cout << "[Login] unknown type=" << type << "\n";
			return true;
		}
	}

	switch (type)
	{
	case static_cast<std::uint16_t>(pt_world::WorldS2CMsg::enter_world_result):
		{
			auto* res = proto::as<pt_world::S2C_enter_world_result>(pMsg, body_len);
			if (!res) return false;

			std::cout << "[World] enter_world_result sid=" << n
				<< " ok=" << static_cast<int>(res->ok)
				<< " account_id=" << res->account_id
				<< " char_id=" << res->char_id
				<< "\n";
		}
		return true;
	case static_cast<std::uint16_t>(pt_world::WorldS2CMsg::kick_notify):
		{
			auto* res = proto::as<pt_world::S2C_kick_notify>(pMsg, body_len);
			if (!res) return false;

			std::cout << "[World] kick_notify sid=" << n
				<< " reason=" << res->reason
				<< " char_id=" << res->char_id
				<< "\n";
		}
		return true;
	case proto::S2CMsg::open_world_success:
		{
			auto* req = proto::as < proto::S2C_open_world_success>(pMsg, body_len);
			if (!req) return false;

			std::cout << "[World] recv open_world_success sid=" << n << "\n";
		}
		return true;
	case proto::S2CMsg::add_gold_ok:
		{
			auto* res = proto::as<proto::S2C_add_gold_ok>(pMsg, body_len);
			if (!res) return false;

			std::cout << "[World] recv add_gold_ok sid=" << n
				<< " ok=" << res->ok
				<< " gold=" << res->gold
				<< "\n";
		}
		return true;

	case proto::S2CMsg::stats:
		{
			auto* res = proto::as<proto::S2C_stats>(pMsg, body_len);
			if (!res) return false;
			std::cout << "[World] stats sid=" << n
				<< " char_id=" << res->char_id
				<< " hp=" << res->hp << "/" << res->max_hp
				<< " atk=" << res->atk
				<< " def=" << res->def
				<< " gold=" << res->gold
				<< "\n";
		}
		return true;
	case proto::S2CMsg::spawn_monster_ok:
		{
			auto* res = proto::as<proto::S2C_spawn_monster_ok>(pMsg, body_len);
			if (!res) return false;
			std::cout << "[World] spawn_monster_ok sid=" << n
				<< " monster_id=" << res->monster_id
				<< " hp=" << res->hp
				<< " atk=" << res->atk
				<< " def=" << res->def
				<< "\n";
		}
		return true;
	case proto::S2CMsg::attack_result:
		{
			auto* res = proto::as<proto::S2C_attack_result>(pMsg, body_len);
			if (!res) return false;
			std::cout << "[World] attack_result sid=" << n
				<< " attacker=" << res->attacker_id
				<< " target=" << res->target_id
				<< " dmg=" << res->damage
				<< " target_hp=" << res->target_hp
				<< " killed=" << res->killed
				<< " drop_item=" << res->drop_item_id
				<< " drop_cnt=" << res->drop_count
				<< " attacker_gold=" << res->attacker_gold
				<< "\n";
		}
		return true;
	case proto::S2CMsg::actor_bound:
		{
			auto* res = proto::as<proto::S2C_actor_bound>(pMsg, body_len);
			if (!res) return false;
			actor_id_.store(res->actor_id, std::memory_order_relaxed);
			{
				std::lock_guard lk(ready_mtx_);
				ready_.store(true, std::memory_order_relaxed);
			}
			ready_cv_.notify_all();
			std::cout << "[World] actor_bound sid=" << n << " actor_id=" << res->actor_id << "\n";
		}
		return true;
	case proto::S2CMsg::actor_seq_ack:
		{
			auto* res = proto::as<proto::S2C_actor_seq_ack>(pMsg, body_len);
			if (!res) return false;
			if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[World] actor_seq_ack sid=" << n
					<< " ok=" << res->ok
					<< " seq/tag=" << res->seq
					<< " shard=" << res->shard
					<< " errors=" << res->errors
					<< "\n";
			}
		}
		return true;

	case proto::S2CMsg::bench_move_ack:
		{
			auto* res = proto::as<proto::S2C_bench_move_ack>(pMsg, body_len);
			if (!res) return false;

			recv_ack_.fetch_add(1, std::memory_order_relaxed);
			const auto now = std::chrono::steady_clock::now().time_since_epoch();
			const long long now_ns = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
			const long long rtt_ns = now_ns - (long long)res->client_ts_ns;
			rtt_sum_ns_.fetch_add(rtt_ns, std::memory_order_relaxed);

			// min/max CAS
			{
				long long cur = rtt_min_ns_.load(std::memory_order_relaxed);
				if (cur == 0 || rtt_ns < cur) {
					while (!rtt_min_ns_.compare_exchange_weak(cur, rtt_ns, std::memory_order_relaxed)) {
						if (cur != 0 && rtt_ns >= cur) break;
					}
				}
			}
			{
				long long cur = rtt_max_ns_.load(std::memory_order_relaxed);
				if (rtt_ns > cur) {
					while (!rtt_max_ns_.compare_exchange_weak(cur, rtt_ns, std::memory_order_relaxed)) {
						if (rtt_ns <= cur) break;
					}
				}
			}

			if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Bench] move_ack sid=" << n
					<< " seq=" << res->seq
					<< " rtt_ms=" << (double)rtt_ns / 1'000'000.0
					<< " zone=" << res->zone
					<< " shard=" << res->shard
					<< "\n";
			}
		}
		return true;

	case proto::S2CMsg::player_spawn:
		{
			auto* res = proto::as<proto::S2C_player_spawn>(pMsg, body_len);
			if (!res) return false;
			recv_spawn_.fetch_add(1, std::memory_order_relaxed);
			/*if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Zone] player_spawn sid=" << n
					<< " char_id=" << res->char_id
					<< " pos=(" << res->x << "," << res->y << ")"
					<< "\n";
			}*/
		}
		return true;
	case proto::S2CMsg::player_despawn:
		{
			auto* res = proto::as<proto::S2C_player_despawn>(pMsg, body_len);
			if (!res) return false;
			recv_despawn_.fetch_add(1, std::memory_order_relaxed);
			/*if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Zone] player_despawn sid=" << n
					<< " char_id=" << res->char_id
					<< "\n";
			}*/
		}
		return true;
	case proto::S2CMsg::player_move_batch:
		{
			// body: proto::S2C_player_move_batch (flexible array)
			// - u16 count + items[count]
			if (!pMsg || body_len < sizeof(std::uint16_t)) return false;
			std::uint16_t count = 0;
			std::memcpy(&count, pMsg, sizeof(std::uint16_t));
			if (count == 0) {
				// 서버는 보통 count==0을 보내지 않지만, 방어적으로 허용
				return true;
			}

			auto* res = proto::as<proto::S2C_player_move_batch>(pMsg, body_len);
			if (!res) return false;
			// 실제 필요 바이트: sizeof(batch) + (count-1)*sizeof(item)
			const std::size_t need = sizeof(proto::S2C_player_move_batch)
				+ (std::size_t)(count - 1) * sizeof(proto::S2C_player_move_item);
			if (body_len < need) return false;
			recv_move_pkts_.fetch_add(1, std::memory_order_relaxed);
			recv_move_items_.fetch_add(count, std::memory_order_relaxed);
			return true;
		}
	case proto::S2CMsg::player_move:
		{
			auto* res = proto::as<proto::S2C_player_move>(pMsg, body_len);
			if (!res) return false;
			recv_move_pkts_.fetch_add(1, std::memory_order_relaxed);
			recv_move_items_.fetch_add(1, std::memory_order_relaxed);
			/*if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Zone] player_move sid=" << n
					<< " char_id=" << res->char_id
					<< " pos=(" << res->x << "," << res->y << ")"
					<< "\n";
			}*/
		}
		return true;
	default:
		std::cout << "[World] unknown type=" << type << "\n";
		return true;
	}
}
