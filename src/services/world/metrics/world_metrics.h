#pragma once

#include <atomic>
#include <cstdint>

namespace svr::metrics {

	inline std::atomic<std::uint64_t> g_s2c_move_pkts_sent{ 0 };;
	inline std::atomic<std::uint64_t> g_s2c_move_items_sent{ 0 };;
	inline std::atomic<std::uint64_t> g_c2s_bench_move_rx{ 0 };      // C2S bench_move packets received
	inline std::atomic<std::uint64_t> g_s2c_bench_ack_tx{ 0 };       // S2C bench_move_ack successfully sent
	inline std::atomic<std::uint64_t> g_s2c_bench_ack_drop{ 0 };     // S2C bench_move_ack dropped (lossy send failed)
	inline std::atomic<std::uint64_t> g_aoi_entered_entities{ 0 };   // AOI entered entities per move aggregation
	inline std::atomic<std::uint64_t> g_aoi_exited_entities{ 0 };    // AOI exited entities per move aggregation
	inline std::atomic<std::uint64_t> g_aoi_move_fanout{ 0 };        // number of move recipients
	inline std::atomic<std::uint64_t> g_aoi_move_events{ 0 };        // number of move events processed
	inline std::atomic<std::uint64_t> g_world_unauth_packet_rejects{ 0 }; // packet rejected because session is not authenticated
	inline std::atomic<std::uint32_t> g_world_unauth_last_sid{ 0 }; // sampled sid for unauth packet
	inline std::atomic<std::uint64_t> g_dup_login_char{ 0 };
	inline std::atomic<std::uint64_t> g_dup_login_account{ 0 };
	inline std::atomic<std::uint64_t> g_dup_login_both{ 0 };
	inline std::atomic<std::uint64_t> g_dup_login_dedup_same_session{ 0 };
	inline std::atomic<std::uint64_t> g_flush_dirty_conflicts_total{ 0 };
	inline std::atomic<std::uint64_t> g_flush_dirty_conflicted_batches{ 0 };
	inline std::atomic<std::uint32_t> g_flush_dirty_conflict_world_sample{ 0 };
	inline std::atomic<std::uint32_t> g_flush_dirty_conflict_shard_sample{ 0 };
	inline std::atomic<std::uint64_t> g_flush_dirty_conflict_char_sample{ 0 };

} // namespace svr::metrics
