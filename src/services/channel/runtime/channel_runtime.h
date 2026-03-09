#pragma once
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
#include <boost/asio/executor_work_guard.hpp>
#include "core/metrics/proc_metrics.h"

#include "net/tcp/tcp_server.h"
#include "net/actor/actor_system.h"

#include "db/core/dqs_types.h"
#include "db/core/dqs_results.h"
#include "db/odbc/odbc_wrapper.h"
#include "db/shard/db_shard_manager.h"
#include "cache/redis/redis_cache.h"
#include "services/channel/handler/channel_handler.h"
#include "services/channel/actors/channel_actors.h"
#include "services/channel/runtime/channel_session_registry.h"


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

	// 원본 LogServer 포트 기본값(필요시 ini로 교체)
	constexpr std::uint16_t PORT_LOG_WORLD = 27787;
	constexpr std::uint16_t PORT_LOG_LOGIN = 27788;
	constexpr std::uint16_t PORT_LOG_CONTROL = 27789;

	class ChannelRuntime final {
	public:
		ChannelRuntime();
		~ChannelRuntime();

		bool InitMainThread();
		void ReleaseMainThread();
		void MainLoop();

		// ✅ Actor 모델: actor_id(= char_id 등) 단위로 순서+단일 실행 보장
		void PostActor(std::uint64_t actor_id, std::function<void()> fn);
		// 전역(관리) Actor
		void Post(std::function<void()> fn);

		// ✅ (Actor 워커 스레드에서만) Actor 인스턴스 접근
		// - 케이스1(Actor당 상태 인스턴스) 구현용
		PlayerActor& GetOrCreatePlayerActor(std::uint64_t char_id);
		WorldActor& GetOrCreateWorldActor();
		void EraseActor(std::uint64_t actor_id);
		ZoneActor& GetOrCreateZoneActor(std::uint32_t zone_id);

		// ✅ DQS 결과는 무조건 메인으로 합류 (결과 객체 패턴)
		void PostDqsResult(svr::dqs_result::Result r);

		// ✅ 세션(index) <-> char_id 바인딩 저장소
		// - Handler는 라우팅 판단만 하고, 실제 저장 책임은 Runtime이 가진다.
		std::uint64_t FindCharIdBySession(std::uint32_t sid) const;
		void BindSessionCharId(std::uint32_t sid, std::uint64_t char_id);
		std::uint64_t UnbindSessionCharId(std::uint32_t sid);

		// 원본 CloseWorldServer에 해당(레거시 socketIndex -> 여기서는 sid로 씀)
		void CloseWorldServer(std::uint32_t world_socket_index);

		// 슬롯풀 + 인덱스 큐
		bool PushDQSData(std::uint8_t process_code, std::uint8_t qry_case, const char* data, int size);

		// ===== Redis write-behind (A안) API =====
		// 캐릭터 상태 변경 시 호출: Redis에 저장 + dirty 마킹
		void CacheCharacterState(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob);

		// ✅ 샘플: 접속 시 redis에서 상태 로드(있으면 반환)
		std::optional<std::string> TryLoadCharacterState(std::uint32_t world_code, std::uint64_t char_id);

		// 로그아웃(또는 강제 저장) 시 호출: 해당 캐릭터를 즉시 DB로 flush
		void RequestFlushCharacter(std::uint32_t world_code, std::uint64_t char_id);

		// ===== Bench measurement control (triggered by client commands) =====
		// - thread-safe: can be called from Actor threads.
		void RequestBenchReset() noexcept;
		void RequestBenchMeasure(int seconds) noexcept;

	private:
		bool LoadIniFile();   // 스텁
		bool DatabaseInit();  // 스텁
		bool NetworkInit();   // Asio 서버 3개 기동

		bool InitRedis();     // Redis 연결
		void ScheduleFlush_();
		void EnqueueFlushDirty_(bool immediate);
		void EnqueueFlushDirtyWorld_(std::uint32_t world_code, std::uint32_t batch); // ✅ 메인에서만 호출

		// ---- DQS (A안) ----
		bool InitDQS();
		void OnDQSRunOne(std::uint32_t slot_index);
		std::uint32_t RouteShard_(std::uint8_t qry_case, const char* data, int size) const;

		// ✅ 메인 스레드에서만 실행되는 DQS 결과 처리
		void HandleDqsResult_(const svr::dqs_result::Result& r);

		// bench helpers (main thread only)
		void BenchResetMain_();
		void BenchStartMain_(int seconds);
		void BenchTickMain_();
	private:
		// ✅ Actor 런타임(로직 실행기)
		// - actor_id(key)별 메시지 순서 보장 + shard 병렬 실행
		net::ActorSystem actors_;

		// ✅ 세션(index) <-> char_id 바인딩 저장소
		ChannelSessionRegistry session_registry_;

		// ✅ Actor(로직) 워커 스레드 수(ini)
		int logic_thread_count_ = 1;

		// io_context는 네트워크 전용 스레드 풀에서 run()
		int io_thread_count_ = 1;
		std::vector<std::jthread> io_threads_;
		using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
		std::optional<WorkGuard> work_guard_;

		boost::asio::io_context io_{ 1 };
		boost::asio::steady_timer flush_timer_{ io_ };

		std::unique_ptr<cache::RedisCache> redis_cache_;
		std::unique_ptr<net::TcpServer> world_server_;
		std::unique_ptr<net::TcpServer> login_server_;
		std::unique_ptr<net::TcpServer> control_server_;

		// 중요: 서버별로 handler를 분리(라인/ProID 구분)
		std::shared_ptr<ChannelHandler> world_handler_;
		std::shared_ptr<ChannelHandler> login_handler_;
		std::shared_ptr<ChannelHandler> control_handler_;

		// 슬롯풀 + 인덱스 큐
		static constexpr std::uint32_t MAX_DB_SYC_DATA_NUM = 200000; // 필요시 조정
		std::vector<svr::dqs::DqsSlot> dqs_slots_;
		std::mutex dqs_mtx_;
		std::deque<std::uint32_t> dqs_empty_;
		std::atomic<std::uint64_t> dqs_drop_count_{ 0 };


		// ✅ DB shard workers
		// ✅ DB/DQS 샤드 워커 개수(ini)
		std::uint32_t db_shard_count_ = 1;   // 1이면 기존 단일 워커와 동일
		std::unique_ptr<svr::dbshard::DbShardManager> db_shards_;

		std::atomic<bool> running_{ false };

		// ===== bench window state (main thread loop owns) =====
		std::atomic<bool> bench_req_reset_{ false };
		std::atomic<int> bench_req_measure_seconds_{ 0 };

		bool bench_active_ = false;
		int bench_target_seconds_ = 0;
		int bench_elapsed_seconds_ = 0;
		std::chrono::steady_clock::time_point bench_start_tp_{};
		std::chrono::steady_clock::time_point bench_next_tick_{};

		// baselines for delta calc
		std::uint64_t bench_base_c2s_move_rx_ = 0;
		std::uint64_t bench_base_s2c_ack_tx_ = 0;
		std::uint64_t bench_base_s2c_ack_drop_ = 0;
		std::uint64_t bench_base_s2c_move_pkts_ = 0;
		std::uint64_t bench_base_s2c_move_items_ = 0;
		std::uint64_t bench_base_send_drops_ = 0;

		// totals within window
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

		std::uint16_t port_world_ = PORT_LOG_WORLD;
		std::uint16_t port_login_ = PORT_LOG_LOGIN;
		std::uint16_t port_control_ = PORT_LOG_CONTROL;

		// DB 계정
		std::string db_acc_;
		std::string db_pw_;

		// Redis 설정 (ini에서 읽음)
		std::string redis_host_ = "127.0.0.1";
		int redis_port_ = 6379;
		int redis_db_ = 0;
		std::string redis_password_;
		std::string redis_prefix_ = "dc";

		// ✅ Redis "DB급" 옵션 + 샤딩(= dirty set shard 분리)
		std::uint32_t redis_shard_count_ = 0; // 0 means "auto"
		int redis_wait_replicas_ = 0;      // 0이면 WAIT 안 함
		int redis_wait_timeout_ms_ = 0;

		// ✅ write-behind 튜닝값(ini)
		int flush_interval_sec_ = 60; // ✅ ini로 조절
		std::uint32_t flush_batch_immediate_ = 500;
		std::uint32_t flush_batch_normal_ = 200;
		int char_ttl_sec_ = 60 * 60 * 24 * 7; // 기본 7일

		int worldset_num_ = 0;
		std::vector<WorldInfo> worlds_;

		// NET_WORK
		std::uint32_t world_to_log_recv_buffer_size_ = 10'000'000; // ini 기본값

		std::vector<std::unique_ptr<DbPool>> world_pools_;
		int db_pool_size_per_world_ = 2;     // ✅ 
	};

	extern ChannelRuntime g_Main;

	void ServerProgramExit(const char* call_site, bool save);

} // namespace logsvr
