#pragma once

#include <atomic>
#include <cstdint>

namespace svr {

	// ===== Bench / net hot-path stats (server-side) =====
	// - Reset is triggered by client bench_reset / bench_measure.
	// - Sampling is done in main thread window to match client bench_measure output.

	inline std::atomic<std::uint64_t> g_c2s_bench_move_rx{ 0 };      // C2S bench_move packets received
	inline std::atomic<std::uint64_t> g_s2c_bench_ack_tx{ 0 };       // S2C bench_move_ack successfully sent
	inline std::atomic<std::uint64_t> g_s2c_bench_ack_drop{ 0 };     // S2C bench_move_ack dropped (lossy send failed)

	// NOTE: move broadcast metrics already exist in actors.h
	//   - g_s2c_move_pkts_sent
	//   - g_s2c_move_items_sent

	inline void BenchResetAllServerCounters() noexcept
	{
		g_c2s_bench_move_rx.store(0, std::memory_order_relaxed);
		g_s2c_bench_ack_tx.store(0, std::memory_order_relaxed);
		g_s2c_bench_ack_drop.store(0, std::memory_order_relaxed);
		// actors.h counters are reset by the caller (to keep include dependencies minimal)
	}


} // namespace svr
