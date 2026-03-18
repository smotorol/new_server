#pragma once

#include <atomic>
#include <cstdint>
#include "services/world/metrics/world_metrics.h"

namespace svr {

	// NOTE: move broadcast metrics already exist in actors.h
	//   - g_s2c_move_pkts_sent
	//   - g_s2c_move_items_sent

	inline void BenchResetAllServerCounters() noexcept
	{
		svr::metrics::g_c2s_bench_move_rx.store(0, std::memory_order_relaxed);
		svr::metrics::g_s2c_bench_ack_tx.store(0, std::memory_order_relaxed);
		svr::metrics::g_s2c_bench_ack_drop.store(0, std::memory_order_relaxed);
		// actors.h counters are reset by the caller (to keep include dependencies minimal)
	}


} // namespace svr
