#include "channel_runtime.h"

#include <iomanip>
#include <sstream>

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

namespace svr {
	namespace {
		std::string fmt_u64(std::uint64_t v) { return std::to_string(v); }
		std::string fmt_d(double v) { std::ostringstream oss; oss << std::fixed << std::setprecision(3) << v; return oss.str(); }
		void SaveServerMeasureRow(int seconds, double elapsed_sec, std::size_t conns,
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
	} // namespace

	ChannelRuntime g_Main;

	ChannelRuntime::ChannelRuntime() = default;

	ChannelRuntime::~ChannelRuntime() {
		ReleaseMainThread();
	}

	bool ChannelRuntime::InitMainThread() {
		if (running_.exchange(true)) return true;

		if (!LoadIniFile()) {
			spdlog::warn("LoadIniFile() failed (stub). Continue with defaults.");
		}

		if (!DatabaseInit()) {
			spdlog::warn("DatabaseInit() failed (stub). Continue without DB.");
		}

		if (!InitRedis()) {
			spdlog::warn("InitRedis() failed. Continue without Redis (write-behind disabled).");
		}

		if (!InitDQS()) {
			spdlog::error("InitDQS() failed.");
			running_ = false;
			return false;
		}

		// ✅ DB Shard workers start (N개)
		db_shards_ = std::make_unique<svr::dbshard::DbShardManager>(
			db_shard_count_,
			[this](std::uint32_t idx) { OnDQSRunOne(idx); },
			[this](std::uint32_t idx) {
			// 슬롯 반납(중요)
			std::lock_guard lk(dqs_mtx_);
			dqs_slots_[idx].reset();
			dqs_empty_.push_back(idx);
		});
		db_shards_->start();

		// ✅ Actor(로직) 워커 스레드 시작
		actors_.start((std::uint32_t)std::max(1, logic_thread_count_));

		if (!NetworkInit()) {
			spdlog::error("NetworkInit() failed.");
			running_ = false;
			return false;
		}

		// Redis write-behind: 60초 주기 flush 스케줄링 + 재기동 시 복구 flush
		ScheduleFlush_();
		EnqueueFlushDirty_(/*immediate=*/true);

		// ✅ io_context가 "할 일이 잠시 없어도" 멈추지 않도록 work_guard 유지
		work_guard_.emplace(boost::asio::make_work_guard(io_));

		// ✅ 네트워크 io_context는 스레드 풀에서 run() (메인 스레드는 커맨드 큐만 처리)
		io_threads_.clear();
		io_threads_.reserve((std::size_t)std::max(1, io_thread_count_));
		for (int i = 0; i < std::max(1, io_thread_count_); ++i) {
			io_threads_.emplace_back([this](std::stop_token) { io_.run(); });
		}

		spdlog::info("LogServer initialized. ports: world={}, login={}, control={}",
			port_world_, port_login_, port_control_);
		return true;
	}

	void ChannelRuntime::ReleaseMainThread() {
		if (!running_.exchange(false)) return;

		if (db_shards_) {
			db_shards_->stop();
			db_shards_.reset();
		}

		// ✅ Actor(로직) 워커 종료
		actors_.stop();

		// ✅ io_context 종료
		io_.stop();
		work_guard_.reset();
		for (auto& t : io_threads_) {
			if (t.joinable()) {
				t.request_stop();
				t.join();
			}
		}
		io_threads_.clear();

		control_server_.reset();
		login_server_.reset();
		world_server_.reset();

		control_handler_.reset();
		login_handler_.reset();
		world_handler_.reset();

		spdlog::info("LogServer released.");
	}

	void ChannelRuntime::PostActor(std::uint64_t actor_id, std::function<void()> fn)
	{
		if (!fn) return;
		actors_.post(actor_id, std::move(fn));
	}

	// ============================================================
	// ✅ AOI/섹터 설정(ini)
	//  - eos_royal의 sector_container_.init_sector(rc_size_, WORLD_SIGHT_UNIT) 감성
	//  - ZoneActor 생성 시 여기 값을 사용해서 섹터 시스템을 초기화한다.
	// ============================================================
	struct AoiIniConfig {
		svr::Vec2i map_size{ 2000, 2000 };
		int world_sight_unit = svr::ZoneActor::kCellSize;
		int aoi_radius_cells = 1; // 1 => 3x3
	};
	static AoiIniConfig g_aoi_ini_cfg{};

	static std::unique_ptr<net::IActor> MakeServerActor_(std::uint64_t id)
	{
		if (id == 0) return std::make_unique<svr::WorldActor>();
		if (svr::IsZoneActorId(id)) {
			auto z = std::make_unique<svr::ZoneActor>();
			// ✅ eos_royal 스타일: 섹터 컨테이너 초기화(ini 기반)
			z->InitSectorSystem(g_aoi_ini_cfg.map_size,
				(std::int32_t)g_aoi_ini_cfg.world_sight_unit,
				(std::int32_t)g_aoi_ini_cfg.aoi_radius_cells);
			return z;
		}
		return std::make_unique<svr::PlayerActor>(id);
	}

	svr::PlayerActor& ChannelRuntime::GetOrCreatePlayerActor(std::uint64_t char_id)
	{
		auto& base = actors_.get_or_create_local(char_id, &MakeServerActor_);
		return static_cast<svr::PlayerActor&>(base);
	}

	svr::WorldActor& ChannelRuntime::GetOrCreateWorldActor()
	{
		auto& base = actors_.get_or_create_local(0, &MakeServerActor_);
		return static_cast<svr::WorldActor&>(base);
	}

	svr::ZoneActor& ChannelRuntime::GetOrCreateZoneActor(std::uint32_t zone_id)
	{
		const std::uint64_t zid = svr::MakeZoneActorId(zone_id);
		auto& base = actors_.get_or_create_local(zid, &MakeServerActor_);
		return static_cast<svr::ZoneActor&>(base);
	}

	void ChannelRuntime::EraseActor(std::uint64_t actor_id)
	{
		actors_.erase_actor(actor_id);
	}

	std::uint64_t ChannelRuntime::FindCharIdBySession(std::uint32_t sid) const
	{
		if (sid == 0) {
			return 0;
		}

		{
			std::lock_guard lk(world_session_mtx_);

			auto it = authed_sessions_by_sid_.find(sid);
			if (it != authed_sessions_by_sid_.end()) {
				return it->second.char_id;
			}
		}

		// 2차 diff에서는 기존 registry를 fallback 으로만 유지한다.
		return session_registry_.FindCharIdBySession(sid);
	}

	void ChannelRuntime::BindSessionCharId(std::uint32_t sid, std::uint64_t char_id)
	{
		session_registry_.BindSessionCharId(sid, char_id);
	}

	std::uint64_t ChannelRuntime::UnbindSessionCharId(std::uint32_t sid)
	{
		return session_registry_.UnbindSessionCharId(sid);
	}

	void ChannelRuntime::Post(std::function<void()> fn)
	{
		PostActor(0, std::move(fn));
	}

	void ChannelRuntime::PostDqsResult(svr::dqs_result::Result r)
	{
		// ✅ DQS 결과도 Actor로 라우팅
		std::uint64_t actor_id = 0;
		std::visit([this, &actor_id](auto&& rr) {
			using T = std::decay_t<decltype(rr)>;
			if constexpr (std::is_same_v<T, svr::dqs_result::OpenWorldNoticeResult>) {
				if (world_handler_) actor_id = world_handler_->GetActorIdBySession(rr.sid);
				else actor_id = rr.sid;
			}
			else if constexpr (std::is_same_v<T, svr::dqs_result::FlushOneCharResult>) {
				actor_id = rr.char_id;

			}
			else {
				actor_id = 0;

			}
		}, r);

		PostActor(actor_id, [this, r = std::move(r)]() mutable {
			HandleDqsResult_(r);
		});
	}

	void ChannelRuntime::HandleDqsResult_(const svr::dqs_result::Result& r)
	{
		// ✅ 여기(메인 스레드)에서만:
				// - 게임 상태 변경
				// - 세션 상태 검사
				// - 네트워크 응답(send)
				// - flush 재스케줄/추가 flush 결정
		std::visit([this](auto&& rr) {
			using T = std::decay_t<decltype(rr)>;

			// 1) open_world_notice 결과 처리: 응답(send)은 메인에서만
			if constexpr (std::is_same_v<T, svr::dqs_result::OpenWorldNoticeResult>) {
				if (!world_server_) return;

				proto::S2C_open_world_success res{};
				res.ok = (proto::u32)rr.ok;

				auto h = proto::make_header(
					(std::uint16_t)proto::S2CMsg::open_world_success,
					(std::uint16_t)sizeof(res));

				// ✅ serial 체크 send (stale 응답 자동 drop)
				world_server_->send(rr.sid, rr.serial, h, reinterpret_cast<const char*>(&res));
				return;

			}

			// 2) flush_dirty_chars 결과 처리: 로그 + 추가 flush 결정은 메인에서만
			if constexpr (std::is_same_v<T, svr::dqs_result::FlushDirtyCharsResult>) {
				spdlog::info("[FlushDirtyChars] world={}, pulled={}, saved={}, failed={}, batch={}, result={}",
					rr.world_code, rr.pulled, rr.saved, rr.failed, rr.max_batch, (int)rr.result);

				// ✅ batch만큼 꽉 찼으면 남은 dirty가 더 있을 확률이 높다
				// - 과도 루프 방지 위해: 결과가 꽉 찬 경우에만 “추가 1회” enqueue
				if (rr.max_batch > 0 && rr.pulled >= rr.max_batch) {
					EnqueueFlushDirtyWorld_(rr.world_code, rr.max_batch);

				}
				return;

			}

			// 3) flush_one_char 결과 처리: 로그/추가 후처리(필요시)는 메인에서만
			if constexpr (std::is_same_v<T, svr::dqs_result::FlushOneCharResult>) {
				spdlog::info("[FlushOneChar] world={}, char_id={}, saved={}, result={}",
					rr.world_code, rr.char_id, rr.saved, (int)rr.result);
				return;

			}

		}, r);
	}

	void ChannelRuntime::EnqueueFlushDirtyWorld_(std::uint32_t world_code, std::uint32_t batch)
	{
		if (!redis_cache_) return;
		const std::uint32_t shard_count = db_shards_ ? db_shards_->shard_count() : 1;

		for (std::uint32_t sid = 0; sid < shard_count; ++sid) {
			svr::dqs_payload::FlushDirtyChars payload{};
			payload.world_code = world_code;
			payload.shard_id = sid;
			payload.max_batch = batch;

			(void)PushDQSData(
				(std::uint8_t)svr::dqs::ProcessCode::world,
				(std::uint8_t)svr::dqs::QueryCase::flush_dirty_chars,
				reinterpret_cast<const char*>(&payload),
				(int)sizeof(payload));

		}
	}

	void ChannelRuntime::MainLoop() {
		if (!running_) {
			spdlog::error("MainLoop called but InitMainThread failed.");
			return;
		}

		// ✅ Actor 모델: 로직은 actors_(멀티 로직 스레드)에서 처리된다.
		// main thread는 프로세스 생존/종료 제어 + 간단한 통계 로그용으로만 유지한다.
		auto next_stat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		std::uint64_t last_move_pkts = 0;
		std::uint64_t last_move_items = 0;
		while (running_) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));

			const auto now = std::chrono::steady_clock::now();

			// ===== bench control requests (from Actor threads) =====
			if (bench_req_reset_.exchange(false, std::memory_order_relaxed)) {
				BenchResetMain_();
			}
			const int req_sec = bench_req_measure_seconds_.exchange(0, std::memory_order_relaxed);
			if (req_sec > 0) {
				BenchStartMain_(req_sec);
			}
			if (bench_active_) {
				BenchTickMain_();
			}

			if (now >= next_stat) {
				next_stat = now + std::chrono::seconds(1);
				// 서버→클라 move 브로드캐스트 통계 ("각 세션 enqueue 기준")
				// - 누적 카운터를 유지하고, 여기서만 초당 델타를 구한다.
				const auto cur_pkts = svr::metrics::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
				const auto cur_items = svr::g_s2c_move_items_sent.load(std::memory_order_relaxed);
				const auto d_pkts = cur_pkts - last_move_pkts;
				const auto d_items = cur_items - last_move_items;
				last_move_pkts = cur_pkts;
				last_move_items = cur_items;
				if (d_pkts > 0 || d_items > 0) {
					spdlog::info("[netstats] s2c_move pkts/s={} items/s={}", d_pkts, d_items);
				}
			}
		}
	}

	void ChannelRuntime::CloseWorldServer(std::uint32_t world_socket_index) {
		if (!world_server_) return;
		world_server_->close(world_socket_index);
	}

	bool ChannelRuntime::PushDQSData(std::uint8_t process_code, std::uint8_t qry_case, const char* data, int size) {
		if (!running_ || !data || size <= 0) return false;

		if (size > (int)svr::dqs::DqsSlot::max_data_size)
		{
			dqs_drop_count_++;
			return false;
		}

		std::uint32_t idx = 0;
		{
			std::lock_guard lk(dqs_mtx_);
			if (dqs_empty_.empty())
			{
				dqs_drop_count_++;
				return false;
			}

			idx = dqs_empty_.front();
			dqs_empty_.pop_front();

			auto& slot = dqs_slots_[idx];
			slot.reset(); // ✅ 안전: 이전 상태 제거
			slot.process_code = process_code;
			slot.qry_case = qry_case;
			slot.result = svr::dqs::ResultCode::success;
			slot.in_use = true;
			slot.done = false;
			slot.data_size = (std::uint16_t)size;
			std::memcpy(slot.data.data(), data, (size_t)size);
		}

		// ✅ shard routing → 해당 워커 큐로 푸시
		const std::uint32_t shard = RouteShard_(qry_case, data, size);
		if (db_shards_) {
			db_shards_->push(shard, idx);

		}
		else {
			// ✅ 방어: shard manager가 없으면 즉시 슬롯 반납(누수 방지)
			std::lock_guard lk(dqs_mtx_);
			dqs_slots_[idx].reset();
			dqs_empty_.push_back(idx);
			dqs_drop_count_++;
			return false;

		}
		return true;
	}

	std::uint32_t ChannelRuntime::RouteShard_(std::uint8_t qry_case, const char* data, int size) const
	{
		const std::uint32_t shard_count = db_shards_ ? db_shards_->shard_count() : 1;
		if (shard_count <= 1) return 0;

		const auto qc = (svr::dqs::QueryCase)qry_case;
		if (qc == svr::dqs::QueryCase::flush_one_char) {
			if (size >= (int)sizeof(svr::dqs_payload::FlushOneChar)) {
				svr::dqs_payload::FlushOneChar p{};
				std::memcpy(&p, data, sizeof(p));
				return (std::uint32_t)(p.char_id % shard_count);

			}

		}
		if (qc == svr::dqs::QueryCase::flush_dirty_chars) {
			if (size >= (int)sizeof(svr::dqs_payload::FlushDirtyChars)) {
				svr::dqs_payload::FlushDirtyChars p{};
				std::memcpy(&p, data, sizeof(p));
				return (std::uint32_t)(p.shard_id % shard_count);

			}

		}
		return 0;
	}

	bool ChannelRuntime::LoadIniFile()
	{
		namespace fs = std::filesystem;
		const fs::path ini_path = fs::current_path() / "Initialize" / "ServerSystem.ini";

		std::ifstream is(ini_path, std::ios::in | std::ios::binary);
		if (!is) {
			spdlog::error("INI open failed: {}", ini_path.string());
			return false;
		}

		inipp::Ini<char> ini;
		ini.parse(is);

		// ------------------------------------------------------------
			// 0) local defaults (최종 fallback)
			// ------------------------------------------------------------
		constexpr int kDefaultFlushIntervalSec = 60;
		constexpr std::uint32_t kDefaultBatchImmediate = 500;
		constexpr std::uint32_t kDefaultBatchNormal = 200;
		constexpr int kDefaultCharTtlSec = 60 * 60 * 24 * 7; // 7 days

		// inipp는 기본적으로 trim/escape 처리가 있음
		// ini.default_section(ini.sections[""]);

		// [LOGDB_INFO]
		db_acc_ = ini.sections["DB_INFO"]["Acc"];
		db_pw_ = ini.sections["DB_INFO"]["PW"];

		// [REDIS]
		{
			auto v = ini.sections["REDIS"]["Host"];
			if (!v.empty()) redis_host_ = v;
		}
		{
			auto v = ini.sections["REDIS"]["Port"];
			if (!v.empty()) redis_port_ = std::stoi(v);
		}
		{
			auto v = ini.sections["REDIS"]["DB"];
			if (!v.empty()) redis_db_ = std::stoi(v);
		}
		redis_password_ = ini.sections["REDIS"]["Password"];
		{
			auto v = ini.sections["REDIS"]["Prefix"];
			if (!v.empty()) redis_prefix_ = v;
		}

		// ✅ Redis shard + WAIT 옵션
		{
			auto v = ini.sections["REDIS"]["SHARD_COUNT"];
			if (!v.empty()) redis_shard_count_ = (std::uint32_t)std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["REDIS"]["WAIT_REPLICAS"];
			if (!v.empty()) redis_wait_replicas_ = std::max(0, std::stoi(v));
		}
		{
			auto v = ini.sections["REDIS"]["WAIT_TIMEOUT_MS"];
			if (!v.empty()) redis_wait_timeout_ms_ = std::max(0, std::stoi(v));
		}

		// ✅ write-behind 튜닝
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_INTERVAL_SEC"];
			if (!v.empty()) flush_interval_sec_ = std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_BATCH_IMMEDIATE"];
			if (!v.empty()) flush_batch_immediate_ = (std::uint32_t)std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_BATCH_NORMAL"];
			if (!v.empty()) flush_batch_normal_ = (std::uint32_t)std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["CHAR_TTL_SEC"];
			if (!v.empty()) char_ttl_sec_ = std::max(60, std::stoi(v));
		}

		// ✅ DB/DQS 샤딩 관련
		{
			auto v = ini.sections["DB_WORK"]["DB_POOL_SIZE_PER_WORLD"];
			if (!v.empty()) db_pool_size_per_world_ = std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["DB_WORK"]["DB_SHARD_COUNT"];
			if (!v.empty()) db_shard_count_ = (std::uint32_t)std::max(1, std::stoi(v));
		}

		// [World]
		worldset_num_ = 0;
		{
			auto v = ini.sections["World"]["WorldSet_Num"];
			if (!v.empty()) worldset_num_ = std::stoi(v);
		}

		worlds_.clear();
		worlds_.reserve(std::max(0, worldset_num_));

		for (int i = 0; i < worldset_num_; ++i) {
			WorldInfo w;

			auto key = [&](const char* base) {
				return std::string(base) + std::to_string(i);
			};

			// Name은 한글 가능 → UTF-8 그대로 std::string에 보관하는 게 베스트
			w.name_utf8 = ini.sections["World"][key("Name")];
			w.address = ini.sections["World"][key("Address")];
			w.dsn = ini.sections["World"][key("DSN")];
			w.dbname = ini.sections["World"][key("DBName")];

			{
				auto port = ini.sections["World"][key("Port")];
				w.port = port.empty() ? 0 : std::stoi(port);
			}

			{
				auto idx = ini.sections["World"][key("WorldIdx")];
				w.world_idx = idx.empty() ? 0 : std::stoi(idx);
			}

			worlds_.push_back(std::move(w));
		}

		// [NET_WORK]
		world_to_log_recv_buffer_size_ = 10'000'000;
		{
			auto v = ini.sections["NET_WORK"]["WORLD_TO_LOG_RECV_BUFFER_SIZE"];
			if (!v.empty()) world_to_log_recv_buffer_size_ = (std::uint32_t)std::stoul(v);
		}
		// ✅ io_context run() 스레드 개수(기본 1)
		{
			auto v = ini.sections["NET_WORK"]["IO_THREAD_COUNT"];
			if (!v.empty()) io_thread_count_ = std::max(1, std::stoi(v));
		}

		// ✅ 로직(Actor) 스레드 개수(기본 1)
		{
			auto v = ini.sections["NET_WORK"]["LOGIC_THREAD_COUNT"];
			if (!v.empty()) logic_thread_count_ = std::max(1, std::stoi(v));
		}

		// [AOI] (선택)
		// - MAP_W/MAP_H : 임시 맵 크기(월드/존마다 다르면 확장 가능)
		// - WORLD_SIGHT_UNIT : 셀 단위(레거시 WORLD_SIGHT_UNIT)
		// - AOI_RADIUS_CELLS : 주변 셀 반경(1이면 3x3)
		{
			auto v = ini.sections["AOI"]["MAP_W"];
			if (!v.empty()) g_aoi_ini_cfg.map_size.x = std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["AOI"]["MAP_H"];
			if (!v.empty()) g_aoi_ini_cfg.map_size.y = std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["AOI"]["WORLD_SIGHT_UNIT"];
			if (!v.empty()) g_aoi_ini_cfg.world_sight_unit = std::max(1, std::stoi(v));
		}
		{
			auto v = ini.sections["AOI"]["AOI_RADIUS_CELLS"];
			if (!v.empty()) g_aoi_ini_cfg.aoi_radius_cells = std::max(0, std::stoi(v));
		}

		// ============================================================
		// ✅ normalize / default rules (최종 확정값 계산)
		// ============================================================

		// 1) db shard count: 최소 1
		db_shard_count_ = clamp_u32_min(db_shard_count_, 1u, 1u);

		// 2) redis shard count:
		//    - 0 또는 미설정이면 db_shard_count_로 동기화
		//    - 1 미만이면 1
		if (redis_shard_count_ == 0) {
			redis_shard_count_ = db_shard_count_;

		}
		redis_shard_count_ = clamp_u32_min(redis_shard_count_, 1u, 1u);

		// 3) WAIT: 둘 다 양수일 때만 활성화, 아니면 0으로 통일
		if (!(redis_wait_replicas_ > 0 && redis_wait_timeout_ms_ > 0)) {
			redis_wait_replicas_ = 0;
			redis_wait_timeout_ms_ = 0;

		}

		// 4) flush interval/batch/ttl sanity
		flush_interval_sec_ = clamp_int_min(flush_interval_sec_, 1, kDefaultFlushIntervalSec);
		flush_batch_immediate_ = clamp_u32_min(flush_batch_immediate_, 1u, kDefaultBatchImmediate);
		flush_batch_normal_ = clamp_u32_min(flush_batch_normal_, 1u, kDefaultBatchNormal);
		char_ttl_sec_ = clamp_int_min(char_ttl_sec_, 60, kDefaultCharTtlSec);

		// 5) db pool per world sanity
		db_pool_size_per_world_ = clamp_int_min(db_pool_size_per_world_, 1, 2);

		// 6) AOI/섹터 sanity
		g_aoi_ini_cfg.map_size.x = std::max(1, g_aoi_ini_cfg.map_size.x);
		g_aoi_ini_cfg.map_size.y = std::max(1, g_aoi_ini_cfg.map_size.y);
		g_aoi_ini_cfg.world_sight_unit = std::max(1, g_aoi_ini_cfg.world_sight_unit);
		g_aoi_ini_cfg.aoi_radius_cells = std::max(0, g_aoi_ini_cfg.aoi_radius_cells);


		spdlog::info("INI loaded (UTF-8): acc='{}', worldset_num={}, recv_buf={}",
			db_acc_, worldset_num_, world_to_log_recv_buffer_size_);

		spdlog::info("INI(DB_WORK): pool_per_world={}, db_shards={}", db_pool_size_per_world_, db_shard_count_);
		spdlog::info("INI(WRITE_BEHIND): flush_interval={}s, batch_immediate={}, batch_normal={}, ttl={}s",
			flush_interval_sec_, flush_batch_immediate_, flush_batch_normal_, char_ttl_sec_);
		spdlog::info("INI(REDIS): shard_count={}, wait_replicas={}, wait_timeout_ms={}",
			redis_shard_count_, redis_wait_replicas_, redis_wait_timeout_ms_);
		spdlog::info("INI(AOI): map={}x{}, unit={}, aoi_r_cells={}",
			g_aoi_ini_cfg.map_size.x, g_aoi_ini_cfg.map_size.y,
			g_aoi_ini_cfg.world_sight_unit, g_aoi_ini_cfg.aoi_radius_cells);

		for (const auto& w : worlds_) {
			spdlog::info("World: name='{}' addr='{}' dsn='{}' db='{}' idx={}",
				w.name_utf8, w.address, w.dsn, w.dbname, w.world_idx);
		}

		return true;
	}

	bool ChannelRuntime::DatabaseInit()
	{
		world_pools_.clear();
		if (worldset_num_ <= 0 || worlds_.empty())
		{
			spdlog::warn("DatabaseInit: no world settings. skip.");
			return true;
		}

		world_pools_.resize((size_t)worldset_num_);

		for (int i = 0; i < worldset_num_; ++i)
		{
			const auto& w = worlds_[(size_t)i];
			if (!world_pools_[(size_t)i])
				world_pools_[(size_t)i] = std::make_unique<DbPool>();
			auto& pool = *world_pools_[(size_t)i];

			pool.conns.clear();
			pool.conns.reserve((size_t)db_pool_size_per_world_);

			// 1) 연결 문자열 조합
			std::string dsn_conn =
				"DSN=" + w.dsn +
				";UID=" + db_acc_ +
				";PWD=" + db_pw_ + ";";

			if (!w.dbname.empty()) {
				dsn_conn += "DATABASE=" + w.dbname + ";";
			}

			// 2) DRIVER 기반 fallback 연결 문자열
			std::string driver_conn =
				"DRIVER={ODBC Driver 18 for SQL Server};"
				"SERVER=" + w.address + "," + std::to_string(w.port) + ";"
				"UID=" + db_acc_ + ";"
				"PWD=" + db_pw_ + ";"
				"Encrypt=optional;";

			if (!w.dbname.empty())
				driver_conn += "DATABASE=" + w.dbname + ";";

			try {
				for (int k = 0; k < db_pool_size_per_world_; ++k)
				{
					db::OdbcConnection conn;
					bool connected = false;

					// 1️ DSN 먼저 시도
					try
					{
						conn.connect(dsn_conn);
						connected = conn.connected();
					}
					catch (...)
					{
						spdlog::warn(
							"DSN not found. fallback to DRIVER mode (world={}, dsn={})",
							i, w.dsn);
					}

					// 2️ DRIVER fallback
					if (!connected)
					{
						conn.connect(driver_conn);
						connected = conn.connected();
					}

					if (!connected)
					{
						spdlog::error("DB not connected (world={})", i);
						return false;
					}

					pool.conns.push_back(std::move(conn));
				}
				spdlog::info(
					"DB connected: world={} dsn={} db={} pool={} (dsn+driver fallback)",
					i, w.dsn, w.dbname, db_pool_size_per_world_);
			}
			catch (const db::OdbcError& e) {
				spdlog::error("DB connect failed (dsn={}): {}", w.dsn, e.what());
				return false;
			}
		}

		return true;
	}


	bool ChannelRuntime::NetworkInit() {
		// ✅ 라인 구분을 위해 handler를 3개로 분리
		world_handler_ = std::make_shared<ChannelHandler>(eLine_World);
		login_handler_ = std::make_shared<ChannelHandler>(eLine_Login);
		control_handler_ = std::make_shared<ChannelHandler>(eLine_Control);

		world_server_ = std::make_unique<net::TcpServer>(io_, port_world_, world_handler_);
		login_server_ = std::make_unique<net::TcpServer>(io_, port_login_, login_handler_);
		control_server_ = std::make_unique<net::TcpServer>(io_, port_control_, control_handler_);

		// 기존 ChannelHandler의 Send/Close를 쓰려면 server attach 필요
		world_handler_->AttachServer(world_server_.get());
		login_handler_->AttachServer(login_server_.get());
		control_handler_->AttachServer(control_server_.get());

		// ✅ 네트워크 콜백은 io 스레드에서 발생 -> 메인 스레드 커맨드 큐로 전달
		world_handler_->AttachDispatcher([this](std::uint64_t actor_id,
			std::function<void()> fn) { PostActor(actor_id, std::move(fn)); });
		login_handler_->AttachDispatcher([this](std::uint64_t actor_id,
			std::function<void()> fn) { PostActor(actor_id, std::move(fn)); });
		control_handler_->AttachDispatcher([this](std::uint64_t actor_id,
			std::function<void()> fn) { PostActor(actor_id, std::move(fn)); });

		boost::asio::co_spawn(io_, world_server_->run(), boost::asio::detached);
		boost::asio::co_spawn(io_, login_server_->run(), boost::asio::detached);
		boost::asio::co_spawn(io_, control_server_->run(), boost::asio::detached);

		spdlog::info("NetworkInit: listening world={}, login={}, control={}",
			port_world_, port_login_, port_control_);
		return true;
	}

	bool ChannelRuntime::InitDQS()
	{
		std::lock_guard lk(dqs_mtx_);
		dqs_slots_.assign(MAX_DB_SYC_DATA_NUM, {});
		dqs_empty_.clear();
		for (std::uint32_t i = 0; i < MAX_DB_SYC_DATA_NUM; ++i)
			dqs_empty_.push_back(i);
		return true;
	}

	void ChannelRuntime::OnDQSRunOne(std::uint32_t slot_index)
	{
		auto& slot = dqs_slots_[slot_index];

		// ===== DB 처리 샘플 =====
		// process_code / qry_case에 따라 DB 처리 후, 결과를 네트워크로 응답
		try
		{
			const auto pc = (svr::dqs::ProcessCode)slot.process_code;
			const auto qc = (svr::dqs::QueryCase)slot.qry_case;

			switch (pc)
			{
			case svr::dqs::ProcessCode::world:
				{
					switch (qc)
					{
					case svr::dqs::QueryCase::open_world_notice:
						{
							if (slot.data_size < sizeof(svr::dqs_payload::OpenWorldNotice))
							{
								slot.result = svr::dqs::ResultCode::invalid_data;
								break;
							}

							svr::dqs_payload::OpenWorldNotice payload{};
							std::memcpy(&payload, slot.data.data(), sizeof(payload));

							// 샘플: DB에서 "SELECT 1" 수행 (연결 설정이 없으면 ok=0)
							int ok = 0;
							const std::uint32_t world_code = payload.world_code;
							if (world_code < world_pools_.size() && world_pools_[world_code] && !world_pools_[world_code]->conns.empty())
							{
								auto& conn = world_pools_[world_code]->next();
								ok = conn.execute_scalar_int("SELECT 1");
							}
							else
							{
								ok = 0;
								slot.result = svr::dqs::ResultCode::db_error;
							}

							// ✅ “결과 객체”로 통일: 워커는 결과만 만들고 메인으로 전달
							svr::dqs_result::OpenWorldNoticeResult r{};
							r.world_code = world_code;
							r.sid = payload.sid;
							r.serial = payload.serial;
							r.ok = ok;
							r.result = slot.result;
							PostDqsResult(std::move(r));

						}
						break;
					case svr::dqs::QueryCase::flush_dirty_chars:
						{
							if (!redis_cache_) break;
							if (slot.data_size < sizeof(svr::dqs_payload::FlushDirtyChars))
							{
								slot.result = svr::dqs::ResultCode::invalid_data;
								break;
							}

							svr::dqs_payload::FlushDirtyChars payload{};
							std::memcpy(&payload, slot.data.data(), sizeof(payload));

							const std::uint32_t world_code = payload.world_code;
							const std::uint32_t shard_id = payload.shard_id;
							const std::size_t max_batch = payload.max_batch ? payload.max_batch : 200u;
							std::uint32_t pulled = 0;
							std::uint32_t saved = 0;
							std::uint32_t failed = 0;

							if (world_code >= world_pools_.size() || !world_pools_[world_code] || world_pools_[world_code]->conns.empty())
							{
								slot.result = svr::dqs::ResultCode::db_error;
								svr::dqs_result::FlushDirtyCharsResult r{};
								r.world_code = world_code;
								r.max_batch = (std::uint32_t)max_batch;
								r.pulled = 0; r.saved = 0; r.failed = 0;
								r.result = slot.result;
								PostDqsResult(std::move(r));
								break;
							}

							auto& conn = world_pools_[world_code]->next();

							auto ids = redis_cache_->take_dirty_batch(world_code, shard_id, max_batch);
							pulled = (std::uint32_t)ids.size();
							for (auto char_id : ids)
							{
								auto blob = redis_cache_->get_character_blob(world_code, char_id);
								if (!blob)
									continue;
								try
								{
									db::save_character_blob(conn, char_id, *blob);
									++saved;
								}
								catch (const std::exception& e)
								{
									spdlog::error("FlushDirtyChars DB fail world={}, char_id={}, err={}", world_code, char_id, e.what());
									redis_cache_->mark_dirty(world_code, char_id);
									slot.result = svr::dqs::ResultCode::db_error;
									++failed;
								}
							}

							// ✅ 워커는 결과만 만들고, 로그/재스케줄/추가 flush는 메인에서
							svr::dqs_result::FlushDirtyCharsResult r{};
							r.world_code = world_code;
							r.max_batch = (std::uint32_t)max_batch;
							r.pulled = pulled;
							r.saved = saved;
							r.failed = failed;
							r.result = slot.result;
							PostDqsResult(std::move(r));
						}
						break;
					case svr::dqs::QueryCase::flush_one_char:
						{
							if (!redis_cache_) break;
							if (slot.data_size < sizeof(svr::dqs_payload::FlushOneChar))
							{
								slot.result = svr::dqs::ResultCode::invalid_data;
								break;
							}

							svr::dqs_payload::FlushOneChar payload{};
							std::memcpy(&payload, slot.data.data(), sizeof(payload));

							const std::uint32_t world_code = payload.world_code;
							const std::uint64_t char_id = payload.char_id;
							bool saved = false;

							if (world_code >= world_pools_.size() || !world_pools_[world_code] || world_pools_[world_code]->conns.empty())
							{
								slot.result = svr::dqs::ResultCode::db_error;
								svr::dqs_result::FlushOneCharResult r{};
								r.world_code = world_code;
								r.char_id = char_id;
								r.saved = false;
								r.result = slot.result;
								PostDqsResult(std::move(r));
								break;
							}

							auto blob = redis_cache_->get_character_blob(world_code, char_id);
							if (!blob)
							{
								svr::dqs_result::FlushOneCharResult r{};
								r.world_code = world_code;
								r.char_id = char_id;
								r.saved = false;
								r.result = slot.result;
								PostDqsResult(std::move(r));
								break;
							}

							try
							{
								auto& conn = world_pools_[world_code]->next();
								db::save_character_blob(conn, char_id, *blob);
								redis_cache_->remove_dirty(world_code, char_id);
								saved = true;
							}
							catch (const std::exception& e)
							{
								spdlog::error("FlushOneChar DB fail world={}, char_id={}, err={}", world_code, char_id, e.what());
								redis_cache_->mark_dirty(world_code, char_id);
								slot.result = svr::dqs::ResultCode::db_error;
							}

							svr::dqs_result::FlushOneCharResult r{};
							r.world_code = world_code;
							r.char_id = char_id;
							r.saved = saved;
							r.result = slot.result;
							PostDqsResult(std::move(r));
						}
						break;
					default:
						break;
					}
				}
				break;
			case svr::dqs::ProcessCode::login:
			case svr::dqs::ProcessCode::control:
			default:
				break;

			}
		}
		catch (const db::OdbcError& e)
		{
			spdlog::error("DQS DB error: {}", e.what());
			slot.result = svr::dqs::ResultCode::db_error;
		}
		catch (const std::exception& e)
		{
			spdlog::error("DQS error: {}", e.what());
			slot.result = svr::dqs::ResultCode::invalid_data;
		}

		slot.done = true;

		// 워커에서 즉시 슬롯 반납
		{
			std::lock_guard lk(dqs_mtx_);
			slot.reset();
			dqs_empty_.push_back(slot_index);
		}

	}

	bool ChannelRuntime::InitRedis()
	{
		try
		{
			cache::RedisCache::Config cfg;
			cfg.host = redis_host_;
			cfg.port = redis_port_;
			cfg.db = redis_db_;
			cfg.password = redis_password_;
			cfg.prefix = redis_prefix_;

			// ✅ LoadIniFile에서 이미 normalize된 값
			cfg.shard_count = redis_shard_count_;
			cfg.wait_replicas = redis_wait_replicas_;
			cfg.wait_timeout_ms = redis_wait_timeout_ms_;

			redis_cache_ = std::make_unique<cache::RedisCache>(cfg);

			spdlog::info("Redis connected: {}:{}, db={}, prefix='{}', shard_count={}, wait(repl={}, timeout_ms={})",
				redis_host_, redis_port_, redis_db_, redis_prefix_,
				cfg.shard_count, cfg.wait_replicas, cfg.wait_timeout_ms);
			return true;
		}
		catch (const std::exception& e)
		{
			spdlog::error("Redis init failed: {}", e.what());
			redis_cache_.reset();
			return false;
		}
	}

	void ChannelRuntime::ScheduleFlush_()
	{
		flush_timer_.expires_after(std::chrono::seconds(flush_interval_sec_));
		flush_timer_.async_wait([this](const boost::system::error_code& ec) {
			if (ec || !running_) return;
			// ✅ 스케줄/큐잉 결정은 메인 스레드에서만
			Post([this] {
				EnqueueFlushDirty_(/*immediate=*/false);
				ScheduleFlush_();
			});
		});
	}

	void ChannelRuntime::EnqueueFlushDirty_(bool immediate)
	{
		if (!redis_cache_) return;

		const std::uint32_t batch = immediate ? flush_batch_immediate_ : flush_batch_normal_;
		const std::uint32_t world_count = (std::uint32_t)worlds_.size();
		const std::uint32_t shard_count = db_shards_ ? db_shards_->shard_count() : 1;

		for (std::uint32_t wc = 0; wc < world_count; ++wc)
		{
			for (std::uint32_t sid = 0; sid < shard_count; ++sid) {
				svr::dqs_payload::FlushDirtyChars payload{};
				payload.world_code = wc;
				payload.shard_id = sid;
				payload.max_batch = batch;

				(void)PushDQSData(
					(std::uint8_t)svr::dqs::ProcessCode::world,
					(std::uint8_t)svr::dqs::QueryCase::flush_dirty_chars,
					reinterpret_cast<const char*>(&payload),
					(int)sizeof(payload));

			}
		}
	}

	void ChannelRuntime::CacheCharacterState(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob)
	{
		if (!redis_cache_) return;
		const int ttl = char_ttl_sec_;

		try { redis_cache_->upsert_character(world_code, char_id, blob, ttl); }
		catch (const std::exception& e) { spdlog::error("CacheCharacterState failed: {}", e.what()); }
	}

	std::optional<std::string> ChannelRuntime::TryLoadCharacterState(std::uint32_t world_code, std::uint64_t char_id)
	{
		if (!redis_cache_) return std::nullopt;
		try { return redis_cache_->get_character_blob(world_code, char_id); }
		catch (const std::exception& e)
		{
			spdlog::error("TryLoadCharacterState failed: {}", e.what());
			return std::nullopt;
		}
	}

	void ChannelRuntime::RequestFlushCharacter(std::uint32_t world_code, std::uint64_t char_id)
	{
		if (!redis_cache_) return;

		svr::dqs_payload::FlushOneChar payload{};
		payload.world_code = world_code;
		payload.char_id = char_id;

		(void)PushDQSData(
			(std::uint8_t)svr::dqs::ProcessCode::world,
			(std::uint8_t)svr::dqs::QueryCase::flush_one_char,
			reinterpret_cast<const char*>(&payload),
			(int)sizeof(payload));
	}

	void ChannelRuntime::RequestBenchReset() noexcept
	{
		bench_req_reset_.store(true, std::memory_order_relaxed);
	}

	void ChannelRuntime::RequestBenchMeasure(int seconds) noexcept
	{
		if (seconds <= 0) seconds = 1;
		bench_req_measure_seconds_.store(seconds, std::memory_order_relaxed);
		bench_req_reset_.store(true, std::memory_order_relaxed);
	}

	void ChannelRuntime::BenchResetMain_()
	{
		// reset all server-side bench counters (monotonic)
		svr::BenchResetAllServerCounters();
		svr::metrics::g_s2c_move_pkts_sent.store(0, std::memory_order_relaxed);
		svr::g_s2c_move_items_sent.store(0, std::memory_order_relaxed);

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

	void ChannelRuntime::BenchStartMain_(int seconds)
	{
		if (seconds <= 0) seconds = 1;

		// start new window
		bench_active_ = true;
		bench_target_seconds_ = seconds;
		bench_elapsed_seconds_ = 0;
		bench_start_tp_ = std::chrono::steady_clock::now();
		bench_next_tick_ = bench_start_tp_ + std::chrono::seconds(1);

		// establish baselines (counters are expected to have been reset by BenchResetMain_)
		bench_base_c2s_move_rx_ = svr::g_c2s_bench_move_rx.load(std::memory_order_relaxed);
		bench_base_s2c_ack_tx_ = svr::g_s2c_bench_ack_tx.load(std::memory_order_relaxed);
		bench_base_s2c_ack_drop_ = svr::g_s2c_bench_ack_drop.load(std::memory_order_relaxed);
		bench_base_s2c_move_pkts_ = svr::metrics::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
		bench_base_s2c_move_items_ = svr::g_s2c_move_items_sent.load(std::memory_order_relaxed);
		bench_base_send_drops_ = 0;
		if (world_server_) {
			bench_base_send_drops_ = world_server_->collect_sendq_stats().drops_total;
		}
		bench_proc_prev_ = procmetrics::ReadSelfSnapshot();
		bench_last_rss_bytes_ = bench_proc_prev_.rss_bytes;

		spdlog::info("[bench_measure][server] start seconds={}", seconds);
	}

	void ChannelRuntime::BenchTickMain_()
	{
		const auto now = std::chrono::steady_clock::now();
		if (!bench_active_) return;
		if (now < bench_next_tick_) return;
		bench_next_tick_ += std::chrono::seconds(1);
		bench_elapsed_seconds_ += 1;

		const auto cur_c2s = svr::g_c2s_bench_move_rx.load(std::memory_order_relaxed);
		const auto cur_ack = svr::g_s2c_bench_ack_tx.load(std::memory_order_relaxed);
		const auto cur_ack_drop = svr::g_s2c_bench_ack_drop.load(std::memory_order_relaxed);
		const auto cur_move_pkts = svr::metrics::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
		const auto cur_move_items = svr::g_s2c_move_items_sent.load(std::memory_order_relaxed);

		std::uint64_t cur_send_drops = 0;
		std::uint64_t cur_send_drop_bytes = 0;
		std::uint64_t cur_app_tx_bytes = 0;
		std::uint64_t cur_app_rx_bytes = 0;
		std::size_t qmax_msgs = 0;
		std::size_t qmax_bytes = 0;
		if (world_server_) {
			auto st = world_server_->collect_sendq_stats();
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
			const std::size_t conns = world_server_ ? world_server_->active_session_count() : 0;

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

	void ServerProgramExit(const char* call_site, bool /*save*/) {
		spdlog::warn("ServerProgramExit called from '{}'. stopping...", call_site ? call_site : "(null)");
		g_Main.ReleaseMainThread();
	}

} // namespace logsvr
