#pragma once

#include <atomic>
#include <cstdint>

namespace svr::metrics {

	inline std::atomic<std::uint64_t> g_s2c_move_pkts_sent{ 0 };;
	inline std::atomic<std::uint64_t> g_s2c_move_items_sent{ 0 };;
	inline std::atomic<std::uint64_t> g_c2s_bench_move_rx{ 0 };      // C2S bench_move packets received
	inline std::atomic<std::uint64_t> g_s2c_bench_ack_tx{ 0 };       // S2C bench_move_ack successfully sent
	inline std::atomic<std::uint64_t> g_s2c_bench_ack_drop{ 0 };     // S2C bench_move_ack dropped (lossy send failed)

} // namespace svr::metrics
