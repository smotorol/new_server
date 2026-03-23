#pragma once

#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <chrono>

#include "app/runtime/networkex_base.h"

class CNetworkEX : public dc::NetworkEXBase
{
public:
	struct LoginResultState
	{
		bool ok = false;
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
		std::uint16_t world_port = 0;
		std::string world_host;
		std::string login_session;
		std::string world_token;
	};

	// ---- bench stats ----
	struct BenchSnapshot {
		// move batch 지표 분리
		// - recv_move_pkts : S2C move 관련 패킷 수(배치 포함)
		// - recv_move_items: 패킷 바디에 포함된 move 아이템 수(배치면 count 합)
		std::uint64_t recv_move_pkts = 0;
		std::uint64_t recv_move_items = 0;
		std::uint64_t recv_spawn = 0;
		std::uint64_t recv_despawn = 0;
		std::uint64_t recv_ack = 0;
		double rtt_avg_ms = 0.0;
		double rtt_min_ms = 0.0;
		double rtt_max_ms = 0.0;
	};
public:
	explicit CNetworkEX(std::uint32_t pro_id = 0);
	~CNetworkEX() override = default;

	// ✅ 서버가 알려준 ActorId(char_id)
	std::uint64_t actor_id() const noexcept { return actor_id_.load(std::memory_order_relaxed); }
	bool is_ready() const noexcept { return ready_.load(std::memory_order_relaxed); }
	void wait_ready();
	bool wait_ready_for(std::chrono::milliseconds timeout);
	bool wait_connected_for(std::chrono::milliseconds timeout);
	bool has_login_result() const;
	LoginResultState login_result() const;
	void clear_login_result();

	static void SetBenchQuiet(bool on) noexcept;
	void BenchReset() noexcept;
	BenchSnapshot BenchGetSnapshot() const noexcept;

private:
	std::atomic<std::uint64_t> actor_id_{ 0 };
	std::atomic<bool> ready_{ false };
	std::atomic<bool> connected_{ false };
	mutable std::mutex ready_mtx_;
	std::condition_variable ready_cv_;
	mutable std::mutex connected_mtx_;
	std::condition_variable connected_cv_;

	mutable std::mutex login_result_mtx_;
	LoginResultState login_result_{};
	bool has_login_result_ = false;

	// ready 이전(ActorId 바인딩 전) 분산용 fallback actor id
	std::uint64_t fallback_actor_id_ = 0;
	static std::atomic<std::uint64_t> s_fallback_seq_;

	// bench counters (Actor thread에서 갱신)
	std::atomic<std::uint64_t> recv_move_pkts_{ 0 };
	std::atomic<std::uint64_t> recv_move_items_{ 0 };
	std::atomic<std::uint64_t> recv_spawn_{ 0 };
	std::atomic<std::uint64_t> recv_despawn_{ 0 };
	std::atomic<std::uint64_t> recv_ack_{ 0 };
	std::atomic<long long> rtt_sum_ns_{ 0 };
	std::atomic<long long> rtt_min_ns_{ 0 };
	std::atomic<long long> rtt_max_ns_{ 0 };

	static std::atomic<bool> bench_quiet_;
protected:
	// ✅ test_client: 연결(클라이언트) 단위로 Actor shard 분산을 위해 ActorId 라우팅 키를 재정의
	// - ready 전에는 handler 인스턴스별 고유 fallback_actor_id_ 사용
	// - ready 후(server가 actor_bound 내려준 뒤)에는 actor_id_(char_id) 사용
	std::uint64_t ResolveActorId(std::uint32_t session_idx) const override;
	// ====== 아래는 기존 LogServer의 확장 지점(포팅 대상 함수들) ======
	bool DataAnalysis(std::uint32_t dwProID, std::uint32_t dwClientIndex, _MSG_HEADER* pMsgHeader, char* pMsg) override;
	void AcceptClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;
	void CloseClientCheck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial) override;

	// line별 분석(레거시 구조 유지용)
	bool LineAnalysis(std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg);
};
