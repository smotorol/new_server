#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>
#include <boost/asio.hpp>
#include <unordered_map>

#include "core/metrics/proc_metrics.h"

#include "net/tcp/tcp_server.h"
#include "net/actor/actor_system.h"

#include "db/core/dqs_types.h"
#include "db/core/dqs_results.h"
#include "db/odbc/odbc_wrapper.h"
#include "db/shard/db_shard_manager.h"
#include "cache/redis/redis_cache.h"
#include "services/world/handler/world_handler.h"
#include "services/world/handler/world_login_handler.h"
#include "services/world/handler/world_control_handler.h"
#include "services/world/actors/world_actors.h"
#include "server_common/registry/session_char_registry.h"
#include "server_common/runtime/line_registry.h"
#include "services/runtime/server_runtime_base.h"
#include "services/world/runtime/i_world_runtime.h"
#include "services/world/runtime/world_line_id.h"
#include "proto/client/world_proto.h"
#include "proto/common/packet_util.h"

// World 세팅
struct WorldInfo {
    std::string name_utf8;
    std::string address;
    std::string dsn;
    std::string dbname;
    int port;
    int world_idx = 0;
};

// 월드별 DB Pool(라운드로빈)
struct DbPool
{
    std::vector<db::OdbcConnection> conns;
    std::atomic<std::uint32_t> rr{ 0 };
    db::OdbcConnection& next() { return conns[rr++ % conns.size()]; }
};

namespace svr {

    constexpr std::uint16_t PORT_WORLD = 27787;
    constexpr std::uint16_t PORT_LOGIN = 27788;
    constexpr std::uint16_t PORT_CONTROL = 27789;

    class WorldRuntime final : public dc::ServerRuntimeBase, public IWorldRuntime {
    public:
        WorldRuntime();
        ~WorldRuntime();

        void PostActor(std::uint64_t actor_id, std::function<void()> fn);
        void Post(std::function<void()> fn);

        PlayerActor& GetOrCreatePlayerActor(std::uint64_t char_id);
        WorldActor& GetOrCreateWorldActor();
        void EraseActor(std::uint64_t actor_id);
        ZoneActor& GetOrCreateZoneActor(std::uint32_t zone_id);

        void PostDqsResult(svr::dqs_result::Result r);

        std::uint64_t FindCharIdBySession(std::uint32_t sid) const;
        void BindSessionCharId(std::uint32_t sid, std::uint64_t char_id);
        std::uint64_t UnbindSessionCharId(std::uint32_t sid);

        void CloseWorldServer(std::uint32_t world_socket_index);

        bool PushDQSData(std::uint8_t process_code, std::uint8_t qry_case, const char* data, int size);

        void CacheCharacterState(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob);
        std::optional<std::string> TryLoadCharacterState(std::uint32_t world_code, std::uint64_t char_id);
        void RequestFlushCharacter(std::uint32_t world_code, std::uint64_t char_id);

        void RequestBenchReset() noexcept;
        void RequestBenchMeasure(int seconds) noexcept;

        bool UpsertPendingWorldAuthTicket(
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string token,
            std::uint64_t expire_at_unix_sec);

        bool ConsumePendingWorldAuthTicket(
            std::uint64_t account_id,
            std::uint64_t char_id,
            std::string_view token);

        bool ReplaceWorldSessionForCharWithKick(
            std::uint64_t char_id,
            std::uint32_t new_sid,
            std::uint32_t new_serial,
            std::uint16_t kick_reason);

        void RemoveWorldSessionBinding(
            std::uint64_t char_id,
            std::uint32_t sid,
            std::uint32_t serial);

    private:
        bool OnRuntimeInit() override;
        void OnBeforeIoStop() override;
        void OnAfterIoStop() override;
        void OnMainLoopTick(std::chrono::steady_clock::time_point now) override;

    private:
        void ScheduleDelayedWorldClose_(
            std::uint32_t sid,
            std::uint32_t serial,
            std::chrono::milliseconds delay);

        void CancelAllDelayedWorldCloseTimers_() noexcept;

        bool LoadIniFile();
        bool DatabaseInit();
        bool NetworkInit();
        void InitHostedLineDescriptors_() noexcept;

        bool InitRedis();
        void ScheduleFlush_();
        void EnqueueFlushDirty_(bool immediate);
        void EnqueueFlushDirtyWorld_(std::uint32_t world_code, std::uint32_t batch);

        bool InitDQS();
        void OnDQSRunOne(std::uint32_t slot_index);
        std::uint32_t RouteShard_(std::uint8_t qry_case, const char* data, int size) const;

        void HandleDqsResult_(const svr::dqs_result::Result& r);

        void BenchResetMain_();
        void BenchStartMain_(int seconds);
        void BenchTickMain_();

    private:
        net::ActorSystem actors_;
        dc::SessionCharRegistry session_registry_;
        int logic_thread_count_ = 1;

        boost::asio::steady_timer flush_timer_{ io_ };
        std::mutex delayed_world_close_mtx_;
        std::vector<std::shared_ptr<boost::asio::steady_timer>> delayed_world_close_timers_;


        std::unique_ptr<cache::RedisCache> redis_cache_;

        dc::BasicLineRegistry<svr::WorldLineId, svr::kWorldLineCount> lines_{};

        struct RemoteServiceLineState
        {
            bool registered = false;
            std::uint32_t sid = 0;
            std::uint32_t serial = 0;
            std::uint32_t server_id = 0;
            std::string server_name;
            std::uint16_t listen_port = 0;
        };

        struct PendingWorldAuthTicket
        {
            std::uint64_t account_id = 0;
            std::uint64_t char_id = 0;
            std::string token;
            std::uint64_t expire_at_unix_sec = 0;
        };

        struct WorldSessionRef
        {
            std::uint32_t sid = 0;
            std::uint32_t serial = 0;
        };

        void RegisterLoginLine(
            std::uint32_t sid, std::uint32_t serial,
            std::uint32_t server_id, std::string_view server_name,
            std::uint16_t listen_port);
        void UnregisterLoginLine(
            std::uint32_t sid, std::uint32_t serial);

        void RegisterControlLine(
            std::uint32_t sid, std::uint32_t serial,
            std::uint32_t server_id, std::string_view server_name,
            std::uint16_t listen_port);
        void UnregisterControlLine(
            std::uint32_t sid, std::uint32_t serial);

        mutable std::mutex service_line_mtx_;
        RemoteServiceLineState login_line_state_{};
        RemoteServiceLineState control_line_state_{};

        mutable std::mutex auth_ticket_mtx_;
        std::unordered_map<std::string, PendingWorldAuthTicket> pending_world_auth_tickets_;

        mutable std::mutex world_session_mtx_;
        std::unordered_map<std::uint64_t, WorldSessionRef> world_sessions_by_char_;

        static constexpr std::chrono::milliseconds kDuplicateKickCloseDelay_{ 150 };

        static constexpr std::uint32_t MAX_DB_SYC_DATA_NUM = 200000;
        std::vector<svr::dqs::DqsSlot> dqs_slots_;
        std::mutex dqs_mtx_;
        std::deque<std::uint32_t> dqs_empty_;
        std::atomic<std::uint64_t> dqs_drop_count_{ 0 };

        std::uint32_t db_shard_count_ = 1;
        std::unique_ptr<svr::dbshard::DbShardManager> db_shards_;

        std::atomic<bool> bench_req_reset_{ false };
        std::atomic<int> bench_req_measure_seconds_{ 0 };

        bool bench_active_ = false;
        int bench_target_seconds_ = 0;
        int bench_elapsed_seconds_ = 0;
        std::chrono::steady_clock::time_point bench_start_tp_{};
        std::chrono::steady_clock::time_point bench_next_tick_{};

        std::chrono::steady_clock::time_point next_stat_tp_{};
        std::uint64_t last_move_pkts_ = 0;
        std::uint64_t last_move_items_ = 0;

        std::uint64_t bench_base_c2s_move_rx_ = 0;
        std::uint64_t bench_base_s2c_ack_tx_ = 0;
        std::uint64_t bench_base_s2c_ack_drop_ = 0;
        std::uint64_t bench_base_s2c_move_pkts_ = 0;
        std::uint64_t bench_base_s2c_move_items_ = 0;
        std::uint64_t bench_base_send_drops_ = 0;

        std::uint64_t bench_sum_c2s_move_rx_ = 0;
        std::uint64_t bench_sum_s2c_ack_tx_ = 0;
        std::uint64_t bench_sum_s2c_ack_drop_ = 0;
        std::uint64_t bench_sum_s2c_move_pkts_ = 0;
        std::uint64_t bench_sum_s2c_move_items_ = 0;
        std::uint64_t bench_sum_send_drops_ = 0;
        std::size_t bench_max_sendq_msgs_ = 0;
        std::size_t bench_max_sendq_bytes_ = 0;
        std::uint64_t bench_sum_send_drop_bytes_ = 0;
        std::uint64_t bench_sum_app_tx_bytes_ = 0;
        std::uint64_t bench_sum_app_rx_bytes_ = 0;
        double bench_last_cpu_percent_ = 0.0;
        std::uint64_t bench_last_rss_bytes_ = 0;
        procmetrics::ProcSnapshot bench_proc_prev_{};

        std::uint16_t port_world_ = PORT_WORLD;
        std::uint16_t port_login_ = PORT_LOGIN;
        std::uint16_t port_control_ = PORT_CONTROL;

        std::string db_acc_;
        std::string db_pw_;

        std::string redis_host_ = "127.0.0.1";
        int redis_port_ = 6379;
        int redis_db_ = 0;
        std::string redis_password_;
        std::string redis_prefix_ = "dc";

        std::uint32_t redis_shard_count_ = 0;
        int redis_wait_replicas_ = 0;
        int redis_wait_timeout_ms_ = 0;

        int flush_interval_sec_ = 60;
        std::uint32_t flush_batch_immediate_ = 500;
        std::uint32_t flush_batch_normal_ = 200;
        int char_ttl_sec_ = 60 * 60 * 24 * 7;

        int worldset_num_ = 0;
        std::vector<WorldInfo> worlds_;

        std::uint32_t world_to_log_recv_buffer_size_ = 10'000'000;

        std::vector<std::unique_ptr<DbPool>> world_pools_;
        int db_pool_size_per_world_ = 2;
    };

    extern WorldRuntime g_Main;

    void ServerProgramExit(const char* call_site, bool save);

} // namespace svr
