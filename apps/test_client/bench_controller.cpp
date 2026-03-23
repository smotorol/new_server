#include "bench_controller.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "core/metrics/bench_io.h"
#include "proto/common/proto_base.h"
#include "proto/common/packet_util.h"

#include "define.h"



namespace asio = boost::asio;

BenchController::BenchController(asio::io_context& io)
	: io_(io)
{
}

BenchController::~BenchController()
{
	Teardown();
}

std::uint64_t BenchController::now_ns_()
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

std::pair<int, int> BenchController::coord_of_(int idx, int radius)
{
	const int side = std::max(1, (radius * 2 + 1));
	const int x = (idx % side) - radius;
	const int y = ((idx / side) % side) - radius;
	return { x, y };
}

void BenchController::start_actor_threads_(net::ActorSystem& actors, int conns)
{
	const int hw = (int)std::thread::hardware_concurrency();
	const std::uint32_t actor_threads = (std::uint32_t)std::max(1, std::min(conns, std::max(1, hw)));
	actors.start(actor_threads);
}

std::vector<BenchController::BenchConn> BenchController::spawn_clients_(const std::string& host, std::uint16_t port, int count)
{
	std::vector<BenchConn> out;
	out.reserve((std::size_t)std::max(0, count));
	for (int i = 0; i < count; ++i) {
		BenchConn c;
		c.handler = std::make_shared<CNetworkEX>((std::uint32_t)eLine::world_server);
		c.client = std::make_shared<net::TcpClient>(io_, c.handler);
		c.handler->AttachClient(c.client);
		c.handler->AttachDispatcher([this](std::uint64_t actor_id, std::function<void()> fn) {
			actors_.post(actor_id, std::move(fn));
		});
		c.client->start(host, port);
		out.push_back(std::move(c));
	}
	return out;
}

bool BenchController::Setup(int conns, std::string host, std::uint16_t port)
{
	if (conns <= 0) return false;

	// 기존 벤치가 돌고 있으면 완전히 정리 후 재셋업
	StopWalk();
	Teardown();

	host_ = std::move(host);
	port_ = port;

	start_actor_threads_(actors_, conns);
	{
		std::lock_guard<std::mutex> lk(mtx_);
		conns_ = spawn_clients_(host_, port_, conns);
	}

	// bench_setup은 "연결만" 보장하면 된다.
	// actor_bound(ready)는 enter_world 이후에만 오므로 여기서 wait_ready()를 쓰면 무한 대기가 발생한다.
	constexpr auto kConnectTimeout = std::chrono::seconds(5);
	for (auto& c : conns_) {
		if (!c.handler->wait_connected_for(kConnectTimeout)) {
			std::cout << "[bench_setup] connect timeout (" << kConnectTimeout.count() << "s)\n";
			Teardown();
			return false;
		}
	}

	CNetworkEX::SetBenchQuiet(true);
	ResetStats();

	return true;
}

void BenchController::Teardown()
{
	StopWalk();

	std::lock_guard<std::mutex> lk(mtx_);
	for (auto& c : conns_) {
		if (auto s = c.client->session()) s->close();
	}
	conns_.clear();
	actors_.stop();
}

bool BenchController::StartWalk(int moves_per_sec, int radius, int work_us)
{
	if (moves_per_sec <= 0 || radius < 0 || work_us < 0) return false;

	{
		std::lock_guard<std::mutex> lk(mtx_);
		if (conns_.empty()) return false;
	}

	mps_.store(moves_per_sec, std::memory_order_relaxed);
	radius_.store(radius, std::memory_order_relaxed);
	work_us_.store(work_us, std::memory_order_relaxed);

	StopWalk();
	walk_running_.store(true, std::memory_order_relaxed);
	walk_thread_ = std::jthread([this](std::stop_token st) { walk_loop_(st); });
	return true;
}

void BenchController::StopWalk()
{
	walk_running_.store(false, std::memory_order_relaxed);
	if (walk_thread_.joinable()) {
		walk_thread_.request_stop();
		walk_thread_.join();
	}
}

void BenchController::ResetStats()
{
	sent_.store(0, std::memory_order_relaxed);
	seq_.store(1, std::memory_order_relaxed);

	std::lock_guard<std::mutex> lk(mtx_);
	for (auto& c : conns_) c.handler->BenchReset();

	// ✅ also reset server-side counters so client/server windows match.
	if (!conns_.empty()) {
		auto s = conns_.front().client->session();
		if (s) {
			proto::C2S_bench_reset req{};
			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::bench_reset, (std::uint16_t)sizeof(req));
			s->async_send_lossy(h, reinterpret_cast<const char*>(&req));
		}
	}
}

BenchController::Aggregate BenchController::Snapshot() const
{
	Aggregate a;
	a.sent = sent_.load(std::memory_order_relaxed);

	std::lock_guard<std::mutex> lk(mtx_);
	std::uint64_t move_pkts = 0, move_items = 0;
	std::uint64_t spawn_recv = 0, despawn_recv = 0;
	std::uint64_t ack_total = 0;
	std::uint64_t app_tx_bytes = 0, app_rx_bytes = 0;
	std::uint64_t send_drop_count = 0, send_drop_bytes = 0;
	double rtt_sum_ms = 0.0;
	double rtt_min_ms = 0.0;
	double rtt_max_ms = 0.0;
	bool min_init = false;

	for (auto& c : conns_) {
		auto s = c.handler->BenchGetSnapshot();
		move_pkts += s.recv_move_pkts;
		move_items += s.recv_move_items;
		spawn_recv += s.recv_spawn;
		despawn_recv += s.recv_despawn;
		ack_total += s.recv_ack;
		rtt_sum_ms += s.rtt_avg_ms * (double)s.recv_ack;
		auto sess = c.client->session();
		if (sess) {
			app_tx_bytes += sess->app_tx_bytes();
			app_rx_bytes += sess->app_rx_bytes();
			send_drop_count += sess->send_drop_count();
			send_drop_bytes += sess->send_drop_bytes();
		}
		if (s.recv_ack > 0) {
			if (!min_init || s.rtt_min_ms < rtt_min_ms) rtt_min_ms = s.rtt_min_ms;
			if (!min_init || s.rtt_max_ms > rtt_max_ms) rtt_max_ms = s.rtt_max_ms;
			min_init = true;
		}
	}

	a.ack = ack_total;
	a.recv_move_pkts = move_pkts;
	a.recv_move_items = move_items;
	a.recv_spawn = spawn_recv;
	a.recv_despawn = despawn_recv;
	a.rtt_avg_ms = ack_total > 0 ? (rtt_sum_ms / (double)ack_total) : 0.0;
	a.rtt_min_ms = rtt_min_ms;
	a.rtt_max_ms = rtt_max_ms;
	a.app_tx_bytes = app_tx_bytes;
	a.app_rx_bytes = app_rx_bytes;
	a.send_drop_count = send_drop_count;
	a.send_drop_bytes = send_drop_bytes;
	return a;
}


namespace {
	std::string fmt_u64(std::uint64_t v) { return std::to_string(v); }
	std::string fmt_d(double v) { std::ostringstream oss; oss << std::fixed << std::setprecision(3) << v; return oss.str(); }
	void save_client_measure_row(int seconds, int conns, bool walk_running, const BenchController::Aggregate& a, double elapsed_sec)
	{
		const double sent_per_sec = elapsed_sec > 0.0 ? (double)a.sent / elapsed_sec : 0.0;
		const double ack_per_sec = elapsed_sec > 0.0 ? (double)a.ack / elapsed_sec : 0.0;
		const double move_pkts_per_sec = elapsed_sec > 0.0 ? (double)a.recv_move_pkts / elapsed_sec : 0.0;
		const double move_items_per_sec = elapsed_sec > 0.0 ? (double)a.recv_move_items / elapsed_sec : 0.0;
		const double tx_bps = elapsed_sec > 0.0 ? (double)a.app_tx_bytes / elapsed_sec : 0.0;
		const double rx_bps = elapsed_sec > 0.0 ? (double)a.app_rx_bytes / elapsed_sec : 0.0;
		auto fields = {
			std::pair<std::string, std::string>{"seconds", std::to_string(seconds)},
			{"elapsed_sec", fmt_d(elapsed_sec)},
			{"conns", std::to_string(conns)},
			{"walk_running", walk_running ? "1" : "0"},
			{"sent", fmt_u64(a.sent)},
			{"sent_per_sec", fmt_d(sent_per_sec)},
			{"ack", fmt_u64(a.ack)},
			{"ack_per_sec", fmt_d(ack_per_sec)},
			{"rtt_avg_ms", fmt_d(a.rtt_avg_ms)},
			{"rtt_min_ms", fmt_d(a.rtt_min_ms)},
			{"rtt_max_ms", fmt_d(a.rtt_max_ms)},
			{"recv_move_pkts", fmt_u64(a.recv_move_pkts)},
			{"recv_move_pkts_per_sec", fmt_d(move_pkts_per_sec)},
			{"recv_move_items", fmt_u64(a.recv_move_items)},
			{"recv_move_items_per_sec", fmt_d(move_items_per_sec)},
			{"recv_spawn", fmt_u64(a.recv_spawn)},
			{"recv_despawn", fmt_u64(a.recv_despawn)},
			{"app_tx_bytes", fmt_u64(a.app_tx_bytes)},
			{"app_tx_bytes_per_sec", fmt_d(tx_bps)},
			{"app_rx_bytes", fmt_u64(a.app_rx_bytes)},
			{"app_rx_bytes_per_sec", fmt_d(rx_bps)},
			{"send_drop_count", fmt_u64(a.send_drop_count)},
			{"send_drop_bytes", fmt_u64(a.send_drop_bytes)}
		};
		benchio::AppendTsvRow("bench_client.tsv", fields);
		benchio::AppendCsvRow("bench_client.csv", fields);
	}
} // namespace

BenchController::Aggregate BenchController::MeasureFor(int seconds)
{
	if (seconds <= 0) seconds = 1;
	ResetStats();

	// ✅ ask server to run the same measurement window (and reset at start).
	{
		std::lock_guard<std::mutex> lk(mtx_);
		if (!conns_.empty()) {
			auto s = conns_.front().client->session();
			if (s) {
				proto::C2S_bench_measure req{};
				req.seconds = (proto::u32)seconds;
				auto h = proto::make_header((std::uint16_t)proto::C2SMsg::bench_measure, (std::uint16_t)sizeof(req));
				s->async_send_lossy(h, reinterpret_cast<const char*>(&req));
			}
		}
	}
	const auto t0 = std::chrono::steady_clock::now();
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
	const auto t1 = std::chrono::steady_clock::now();
	const double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();
	auto out = Snapshot();
	save_client_measure_row(seconds, conn_count(), walk_running(), out, elapsed_sec);
	return out;
}

void BenchController::walk_loop_(std::stop_token st)
{
	// 주의:
	// - async_send_*는 내부에서 strand_로 dispatch하므로, 여기서 호출해도 thread-safe
	// - 타이밍은 "tick마다 conns 전체에 한 번씩" 전송하는 방식

	while (!st.stop_requested() && walk_running_.load(std::memory_order_relaxed))
	{
		int mps = mps_.load(std::memory_order_relaxed);
		const int radius = radius_.load(std::memory_order_relaxed);
		const int work_us = work_us_.load(std::memory_order_relaxed);
		if (mps <= 0) mps = 1;

		const auto interval = std::chrono::microseconds(1'000'000 / std::max(1, mps));
		auto next = std::chrono::steady_clock::now() + interval;

		const int tick_toggle = (int)(seq_.load(std::memory_order_relaxed) & 1u);

		std::lock_guard<std::mutex> lk(mtx_);
		for (int i = 0; i < (int)conns_.size(); ++i)
		{
			auto s = conns_[(std::size_t)i].client->session();
			if (!s) continue;

			const auto [bx, by] = coord_of_(i, radius);
			proto::C2S_bench_move req{};
			req.seq = (proto::u32)seq_.fetch_add(1, std::memory_order_relaxed);
			req.work_us = (proto::u32)work_us;
			req.x = bx + tick_toggle;
			req.y = by;
			req.client_ts_ns = (proto::u64)now_ns_();

			auto h = proto::make_header((std::uint16_t)proto::C2SMsg::bench_move, (std::uint16_t)sizeof(req));

			// ✅ 부하 상황에서는 send queue overflow가 발생할 수 있으니,
			//    테스트 세션을 죽이지 않게 lossy send로 보낸다.
			s->async_send_lossy(h, reinterpret_cast<const char*>(&req));
			sent_.fetch_add(1, std::memory_order_relaxed);
		}

		std::this_thread::sleep_until(next);
	}
}
