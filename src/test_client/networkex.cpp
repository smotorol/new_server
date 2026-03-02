#include "networkex.h"
#include <iostream>
#include <cstring>
#include "../proto/proto.h"
#include "../common/string_utils.h"
#include "../proto/packet_util.h"
#include "../net/tcp_client.h"
#include "../net/tcp_session.h"
#include "define.h"
#include "GlobalClients.h"

std::atomic<bool> CNetworkEX::bench_quiet_{ true };

void CNetworkEX::SetBenchQuiet(bool on) noexcept
{
	bench_quiet_.store(on, std::memory_order_relaxed);
}

void CNetworkEX::BenchReset() noexcept
{
	recv_move_.store(0, std::memory_order_relaxed);
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
	s.recv_move = recv_move_.load(std::memory_order_relaxed);
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

bool CNetworkEX::DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg)
{
	switch (static_cast<eLine>(dwProID))
	{
	case eLine::sample_server:
		{
			return LineAnalysis(dwClientIndex, pMsgHeader, pMsg);
		}
		break;
	}
	return false;
}

void CNetworkEX::AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	// 연결되면 open_world_notice 전송
	proto::C2S_open_world_notice req{};

	std::cout << "[client] connected sid=" << dwIndex << "\n";
	copy_cstr(req.szWorldName, "world");

	auto h = proto::make_header((std::uint16_t)proto::C2SMsg::open_world_notice,
		+(std::uint16_t)sizeof(req));

	auto client = GlobalLineClients().client((std::uint8_t)eLine::sample_server);
	auto s = client->session();
	if (s) s->async_send(h, reinterpret_cast<const char*>(&req));

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

	switch (type)
	{
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
			if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Zone] player_spawn sid=" << n
					<< " char_id=" << res->char_id
					<< " pos=(" << res->x << "," << res->y << ")"
					<< "\n";
			}
		}
		return true;
	case proto::S2CMsg::player_despawn:
		{
			auto* res = proto::as<proto::S2C_player_despawn>(pMsg, body_len);
			if (!res) return false;
			recv_despawn_.fetch_add(1, std::memory_order_relaxed);
			if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Zone] player_despawn sid=" << n
					<< " char_id=" << res->char_id
					<< "\n";
			}
		}
		return true;
	case proto::S2CMsg::player_move:
		{
			auto* res = proto::as<proto::S2C_player_move>(pMsg, body_len);
			if (!res) return false;
			recv_move_.fetch_add(1, std::memory_order_relaxed);
			if (!bench_quiet_.load(std::memory_order_relaxed)) {
				std::cout << "[Zone] player_move sid=" << n
					<< " char_id=" << res->char_id
					<< " pos=(" << res->x << "," << res->y << ")"
					<< "\n";
			}
		}
		return true;
	default:
		std::cout << "[World] unknown type=" << type << "\n";
		return true;
	}
}
