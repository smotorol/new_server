#pragma once

#include "services/world/runtime/world_runtime.h"

#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <chrono>
#include <type_traits>
#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <inipp/inipp.h>

#include "core/util/common_utils.h"
#include "core/metrics/bench_io.h"
#include "proto/common/proto_base.h"
#include "proto/common/packet_util.h"
#include "db/core/dqs_payloads.h"
#include "db/core/dqs_results.h"
#include "db/core/db_utils.h"
#include "db/odbc/odbc_wrapper.h"
#include "server_common/runtime/line_start_helper.h"
#include "services/world/common/demo_char_state.h"

namespace pt_w = proto::world;

namespace svr::detail {
    inline constexpr std::uint64_t kConsumedWorldAuthTicketKeepSeconds = 60;

    inline std::string fmt_u64(std::uint64_t v) { return std::to_string(v); }
    inline std::string fmt_d(double v) { std::ostringstream oss; oss << std::fixed << std::setprecision(3) << v; return oss.str(); }
    inline void SaveServerMeasureRow(int seconds, double elapsed_sec, std::size_t conns,
        std::uint64_t c2s_rx, std::uint64_t ack_tx, std::uint64_t ack_drop,
        std::uint64_t move_pkts, std::uint64_t move_items,
        std::uint64_t send_drops, std::uint64_t send_drop_bytes,
        std::size_t sendq_max_msgs, std::size_t sendq_max_bytes,
        std::uint64_t app_tx_bytes, std::uint64_t app_rx_bytes,
        double cpu_percent, std::uint64_t rss_bytes)
    {
        const double c2s_ps = elapsed_sec > 0.0 ? (double)c2s_rx / elapsed_sec : 0.0;
        const double ack_ps = elapsed_sec > 0.0 ? (double)ack_tx / elapsed_sec : 0.0;
        const double move_pkts_ps = elapsed_sec > 0.0 ? (double)move_pkts / elapsed_sec : 0.0;
        const double move_items_ps = elapsed_sec > 0.0 ? (double)move_items / elapsed_sec : 0.0;
        const double send_drops_ps = elapsed_sec > 0.0 ? (double)send_drops / elapsed_sec : 0.0;
        const double tx_bps = elapsed_sec > 0.0 ? (double)app_tx_bytes / elapsed_sec : 0.0;
        const double rx_bps = elapsed_sec > 0.0 ? (double)app_rx_bytes / elapsed_sec : 0.0;
        auto fields = {
            std::pair<std::string, std::string>{"seconds", std::to_string(seconds)},
            {"elapsed_sec", fmt_d(elapsed_sec)},
            {"conns", std::to_string(conns)},
            {"c2s_bench_move_rx", fmt_u64(c2s_rx)},
            {"c2s_bench_move_rx_per_sec", fmt_d(c2s_ps)},
            {"s2c_bench_ack_tx", fmt_u64(ack_tx)},
            {"s2c_bench_ack_tx_per_sec", fmt_d(ack_ps)},
            {"s2c_bench_ack_drop", fmt_u64(ack_drop)},
            {"s2c_move_pkts", fmt_u64(move_pkts)},
            {"s2c_move_pkts_per_sec", fmt_d(move_pkts_ps)},
            {"s2c_move_items", fmt_u64(move_items)},
            {"s2c_move_items_per_sec", fmt_d(move_items_ps)},
            {"sendq_drops", fmt_u64(send_drops)},
            {"sendq_drops_per_sec", fmt_d(send_drops_ps)},
            {"sendq_drop_bytes", fmt_u64(send_drop_bytes)},
            {"sendq_max_msgs", std::to_string(sendq_max_msgs)},
            {"sendq_max_bytes", std::to_string(sendq_max_bytes)},
            {"app_tx_bytes", fmt_u64(app_tx_bytes)},
            {"app_tx_bytes_per_sec", fmt_d(tx_bps)},
            {"app_rx_bytes", fmt_u64(app_rx_bytes)},
            {"app_rx_bytes_per_sec", fmt_d(rx_bps)},
            {"cpu_percent", fmt_d(cpu_percent)},
            {"rss_bytes", fmt_u64(rss_bytes)},
            {"rss_mib", fmt_u64(procmetrics::BytesToMiB(rss_bytes))}
        };
        benchio::AppendTsvRow("bench_server.tsv", fields);
        benchio::AppendCsvRow("bench_server.csv", fields);
    }
} // namespace svr::detail
