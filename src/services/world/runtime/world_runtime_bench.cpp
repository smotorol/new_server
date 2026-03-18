#include "services/world/runtime/world_runtime_private.h"
#include "services/world/metrics/world_metrics.h"

namespace svr {

using namespace svr::detail;


	void WorldRuntime::RequestBenchReset() noexcept
	{
		bench_req_reset_.store(true, std::memory_order_relaxed);
	}



	void WorldRuntime::RequestBenchMeasure(int seconds) noexcept
	{
		if (seconds <= 0) seconds = 1;
		bench_req_measure_seconds_.store(seconds, std::memory_order_relaxed);
		bench_req_reset_.store(true, std::memory_order_relaxed);
	}



	void WorldRuntime::BenchResetMain_()
	{
		// reset all server-side bench counters (monotonic)
		svr::BenchResetAllServerCounters();
		svr::metrics::g_s2c_move_pkts_sent.store(0, std::memory_order_relaxed);
		svr::metrics::g_s2c_move_items_sent.store(0, std::memory_order_relaxed);

		// reset window baselines
		bench_base_c2s_move_rx_ = 0;
		bench_base_s2c_ack_tx_ = 0;
		bench_base_s2c_ack_drop_ = 0;
		bench_base_s2c_move_pkts_ = 0;
		bench_base_s2c_move_items_ = 0;
		bench_base_send_drops_ = 0;
		bench_sum_c2s_move_rx_ = 0;
		bench_sum_s2c_ack_tx_ = 0;
		bench_sum_s2c_ack_drop_ = 0;
		bench_sum_s2c_move_pkts_ = 0;
		bench_sum_s2c_move_items_ = 0;
		bench_sum_send_drops_ = 0;
		bench_max_sendq_msgs_ = 0;
		bench_max_sendq_bytes_ = 0;
		bench_sum_send_drop_bytes_ = 0;
		bench_sum_app_tx_bytes_ = 0;
		bench_sum_app_rx_bytes_ = 0;
		bench_last_cpu_percent_ = 0.0;
		bench_last_rss_bytes_ = 0;
		bench_proc_prev_ = {};
	}



	void WorldRuntime::BenchStartMain_(int seconds)
	{
		if (seconds <= 0) seconds = 1;

		// start new window
		bench_active_ = true;
		bench_target_seconds_ = seconds;
		bench_elapsed_seconds_ = 0;
		bench_start_tp_ = std::chrono::steady_clock::now();
		bench_next_tick_ = bench_start_tp_ + std::chrono::seconds(1);

		// establish baselines (counters are expected to have been reset by BenchResetMain_)
		bench_base_c2s_move_rx_ = svr::metrics::g_c2s_bench_move_rx.load(std::memory_order_relaxed);
		bench_base_s2c_ack_tx_ = svr::metrics::g_s2c_bench_ack_tx.load(std::memory_order_relaxed);
		bench_base_s2c_ack_drop_ = svr::metrics::g_s2c_bench_ack_drop.load(std::memory_order_relaxed);
		bench_base_s2c_move_pkts_ = svr::metrics::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
		bench_base_s2c_move_items_ = svr::metrics::g_s2c_move_items_sent.load(std::memory_order_relaxed);
		bench_base_send_drops_ = 0;
		if (lines_.host(svr::WorldLineId::World).server()) {
			bench_base_send_drops_ = lines_.host(svr::WorldLineId::World).server()->collect_sendq_stats().drops_total;
		}
		bench_proc_prev_ = procmetrics::ReadSelfSnapshot();
		bench_last_rss_bytes_ = bench_proc_prev_.rss_bytes;

		spdlog::info("[bench_measure][server] start seconds={}", seconds);
	}



	void WorldRuntime::BenchTickMain_()
	{
		const auto now = std::chrono::steady_clock::now();
		if (!bench_active_) return;
		if (now < bench_next_tick_) return;
		bench_next_tick_ += std::chrono::seconds(1);
		bench_elapsed_seconds_ += 1;

		const auto cur_c2s = svr::metrics::g_c2s_bench_move_rx.load(std::memory_order_relaxed);
		const auto cur_ack = svr::metrics::g_s2c_bench_ack_tx.load(std::memory_order_relaxed);
		const auto cur_ack_drop = svr::metrics::g_s2c_bench_ack_drop.load(std::memory_order_relaxed);
		const auto cur_move_pkts = svr::metrics::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
		const auto cur_move_items = svr::metrics::g_s2c_move_items_sent.load(std::memory_order_relaxed);

		std::uint64_t cur_send_drops = 0;
		std::uint64_t cur_send_drop_bytes = 0;
		std::uint64_t cur_app_tx_bytes = 0;
		std::uint64_t cur_app_rx_bytes = 0;
		std::size_t qmax_msgs = 0;
		std::size_t qmax_bytes = 0;
		if (lines_.host(svr::WorldLineId::World).server()) {
			auto st = lines_.host(svr::WorldLineId::World).server()->collect_sendq_stats();
			cur_send_drops = st.drops_total;
			cur_send_drop_bytes = st.drop_bytes_total;
			cur_app_tx_bytes = st.app_tx_bytes_total;
			cur_app_rx_bytes = st.app_rx_bytes_total;
			qmax_msgs = st.q_msgs_max;
			qmax_bytes = st.q_bytes_max;
		}
		auto proc_now = procmetrics::ReadSelfSnapshot();
		double cpu_percent = procmetrics::CpuPercentBetween(bench_proc_prev_, proc_now, std::chrono::seconds(1));
		if (proc_now.valid) {
			bench_proc_prev_ = proc_now;
			bench_last_rss_bytes_ = proc_now.rss_bytes;
		}

		const auto d_c2s = cur_c2s - bench_base_c2s_move_rx_;
		const auto d_ack = cur_ack - bench_base_s2c_ack_tx_;
		const auto d_ack_drop = cur_ack_drop - bench_base_s2c_ack_drop_;
		const auto d_move_pkts = cur_move_pkts - bench_base_s2c_move_pkts_;
		const auto d_move_items = cur_move_items - bench_base_s2c_move_items_;
		const auto d_send_drops = cur_send_drops - bench_base_send_drops_;

		bench_base_c2s_move_rx_ = cur_c2s;
		bench_base_s2c_ack_tx_ = cur_ack;
		bench_base_s2c_ack_drop_ = cur_ack_drop;
		bench_base_s2c_move_pkts_ = cur_move_pkts;
		bench_base_s2c_move_items_ = cur_move_items;
		bench_base_send_drops_ = cur_send_drops;

		bench_sum_c2s_move_rx_ += d_c2s;
		bench_sum_s2c_ack_tx_ += d_ack;
		bench_sum_s2c_ack_drop_ += d_ack_drop;
		bench_sum_s2c_move_pkts_ += d_move_pkts;
		bench_sum_s2c_move_items_ += d_move_items;
		bench_sum_send_drops_ += d_send_drops;
		bench_sum_send_drop_bytes_ = cur_send_drop_bytes;
		bench_sum_app_tx_bytes_ = cur_app_tx_bytes;
		bench_sum_app_rx_bytes_ = cur_app_rx_bytes;
		bench_last_cpu_percent_ = cpu_percent;
		bench_max_sendq_msgs_ = std::max<std::size_t>(bench_max_sendq_msgs_, qmax_msgs);
		bench_max_sendq_bytes_ = std::max<std::size_t>(bench_max_sendq_bytes_, qmax_bytes);

		spdlog::info("[bench_sample][server] sec={} c2s_move_rx={} ack_tx={} ack_drop={} s2c_move_pkts={} s2c_move_items={} sendq_drop={} sendq_max_msgs={} sendq_max_bytes={} app_tx_bytes={} app_rx_bytes={} cpu_pct={} rss_mb={}",
			bench_elapsed_seconds_, d_c2s, d_ack, d_ack_drop, d_move_pkts, d_move_items, d_send_drops, qmax_msgs, qmax_bytes, cur_app_tx_bytes, cur_app_rx_bytes, cpu_percent, procmetrics::BytesToMiB(bench_last_rss_bytes_));

		if (bench_elapsed_seconds_ >= bench_target_seconds_) {
			bench_active_ = false;
			const auto t1 = std::chrono::steady_clock::now();
			const double elapsed_sec = std::chrono::duration<double>(t1 - bench_start_tp_).count();
			const std::size_t conns = lines_.host(svr::WorldLineId::World).server() ? lines_.host(svr::WorldLineId::World).server()->active_session_count() : 0;

			// match client's bench_measure style/field names as much as possible.
			spdlog::info("[bench_measure][server] done\n  elapsed_sec={}\n  conns={}\n  c2s_bench_move_rx={} ({} pkt/s)\n  s2c_bench_ack_tx={} ({} ack/s) ack_drop={}\n  zone_broadcast s2c_move_pkts={} ({} pkt/s) s2c_move_items={} ({} items/s)\n  sendq drops={} ({} drops/s) drop_bytes={} sendq_max_msgs={} sendq_max_bytes={}\n  app_tx_bytes={} app_rx_bytes={}\n  cpu_pct={} rss_mb={}",
				elapsed_sec,
				conns,
				bench_sum_c2s_move_rx_, (elapsed_sec > 0.0 ? (double)bench_sum_c2s_move_rx_ / elapsed_sec : 0.0),
				bench_sum_s2c_ack_tx_, (elapsed_sec > 0.0 ? (double)bench_sum_s2c_ack_tx_ / elapsed_sec : 0.0),
				bench_sum_s2c_ack_drop_,
				bench_sum_s2c_move_pkts_, (elapsed_sec > 0.0 ? (double)bench_sum_s2c_move_pkts_ / elapsed_sec : 0.0),
				bench_sum_s2c_move_items_, (elapsed_sec > 0.0 ? (double)bench_sum_s2c_move_items_ / elapsed_sec : 0.0),
				bench_sum_send_drops_, (elapsed_sec > 0.0 ? (double)bench_sum_send_drops_ / elapsed_sec : 0.0), bench_sum_send_drop_bytes_,
				bench_max_sendq_msgs_, bench_max_sendq_bytes_,
				bench_sum_app_tx_bytes_, bench_sum_app_rx_bytes_,
				bench_last_cpu_percent_, procmetrics::BytesToMiB(bench_last_rss_bytes_));
			SaveServerMeasureRow(bench_target_seconds_, elapsed_sec, conns,
				bench_sum_c2s_move_rx_, bench_sum_s2c_ack_tx_, bench_sum_s2c_ack_drop_,
				bench_sum_s2c_move_pkts_, bench_sum_s2c_move_items_,
				bench_sum_send_drops_, bench_sum_send_drop_bytes_,
				bench_max_sendq_msgs_, bench_max_sendq_bytes_,
				bench_sum_app_tx_bytes_, bench_sum_app_rx_bytes_,
				bench_last_cpu_percent_, bench_last_rss_bytes_);
		}
	}


} // namespace svr
