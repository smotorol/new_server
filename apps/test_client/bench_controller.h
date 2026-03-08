#pragma once

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/actor/actor_system.h"
#include "net/tcp/tcp_client.h"

#include "networkex.h"

// test_client용: "동접(연결)"과 "트래픽(이동/ACK)"을 분리해서
// 1) bench_setup 으로 원하는 수의 클라를 먼저 붙여놓고
// 2) bench_walk_start / bench_measure 등으로 그 상태에서 부하 테스트를 수행
class BenchController final
{
public:
	struct BenchConn {
		std::shared_ptr<CNetworkEX> handler;
		std::shared_ptr<net::TcpClient> client;
	};

	struct Aggregate {
		std::uint64_t sent = 0;
		std::uint64_t ack = 0;
		std::uint64_t recv_move_pkts = 0;
		std::uint64_t recv_move_items = 0;
		std::uint64_t recv_spawn = 0;
		std::uint64_t recv_despawn = 0;
		double rtt_avg_ms = 0.0;
		double rtt_min_ms = 0.0;
		double rtt_max_ms = 0.0;
		std::uint64_t app_tx_bytes = 0;
		std::uint64_t app_rx_bytes = 0;
		std::uint64_t send_drop_count = 0;
		std::uint64_t send_drop_bytes = 0;
	};

public:
	explicit BenchController(boost::asio::io_context& io);
	~BenchController();

	BenchController(const BenchController&) = delete;
	BenchController& operator=(const BenchController&) = delete;

	// 연결만 미리 만들어둠(동접 셋업)
	bool Setup(int conns, std::string host, std::uint16_t port);
	void Teardown();

	// 트래픽 생성(무한)
	bool StartWalk(int moves_per_sec, int radius, int work_us);
	void StopWalk();

	// 벤치 카운터 리셋
	void ResetStats();
	Aggregate Snapshot() const;

	// 현재 상태에서 seconds 동안 측정(ResetStats 포함)
	// - walk가 이미 실행 중이면 그대로 두고, 카운터만 reset 후 측정
	Aggregate MeasureFor(int seconds);

	int conn_count() const noexcept { return (int)conns_.size(); }
	bool walk_running() const noexcept { return walk_running_.load(std::memory_order_relaxed); }

private:
	static std::uint64_t now_ns_();
	static std::pair<int, int> coord_of_(int idx, int radius);
	static void start_actor_threads_(net::ActorSystem& actors, int conns);

	std::vector<BenchConn> spawn_clients_(const std::string& host, std::uint16_t port, int count);

	void walk_loop_(std::stop_token st);

private:
	boost::asio::io_context& io_;
	mutable std::mutex mtx_;

	std::string host_ = "127.0.0.1";
	std::uint16_t port_ = 27787;

	net::ActorSystem actors_;
	std::vector<BenchConn> conns_;

	std::atomic<bool> walk_running_{ false };
	std::jthread walk_thread_;

	// walk config
	std::atomic<int> mps_{ 10 };
	std::atomic<int> radius_{ 10 };
	std::atomic<int> work_us_{ 0 };
	std::atomic<std::uint32_t> seq_{ 1 };
	std::atomic<std::uint64_t> sent_{ 0 };
};
