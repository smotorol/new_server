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

namespace svr {
	namespace {
		constexpr std::uint64_t kConsumedWorldAuthTicketKeepSeconds = 60;

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

	WorldRuntime g_Main;

	WorldRuntime::WorldRuntime() = default;

	WorldRuntime::~WorldRuntime() {
		ReleaseMainThread();
	}

	bool WorldRuntime::OnRuntimeInit() {
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
			return false;
		}

		// Redis write-behind: 60초 주기 flush 스케줄링 + 재기동 시 복구 flush
		ScheduleFlush_();
		EnqueueFlushDirty_(true);

		next_stat_tp_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		last_move_pkts_ = 0;
		last_move_items_ = 0;

		spdlog::info("WorldServer initialized. ports: world={}, zone={}, control={}",
			port_world_, port_zone_, port_control_);
		return true;
	}

	void WorldRuntime::OnBeforeIoStop() {
		{
			flush_timer_.cancel();
		}

		CancelDelayedWorldCloseTimers_();

		if (db_shards_) {
			db_shards_->stop();
			db_shards_.reset();
		}

		// ✅ Actor(로직) 워커 종료
		actors_.stop();
	}

	void WorldRuntime::InitHostedLineDescriptors_() noexcept
	{
		lines_.desc(svr::WorldLineId::World) = dc::LineDescriptor{
	static_cast<std::uint32_t>(svr::WorldLineId::World),
	"world",
	port_world_,
	true,
	0
		};

		lines_.desc(svr::WorldLineId::Zone) = dc::LineDescriptor{
			static_cast<std::uint32_t>(svr::WorldLineId::Zone),
			"zone",
			port_zone_,
			true,
			0
		};

		lines_.desc(svr::WorldLineId::Control) = dc::LineDescriptor{
			static_cast<std::uint32_t>(svr::WorldLineId::Control),
			"control",
			port_control_,
			true,
			0
		};
	}

	void WorldRuntime::OnAfterIoStop() {
		lines_.stop_all_reverse();

		{
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			pending_enter_world_consume_.clear();
		}

		{
			std::lock_guard lk(delayed_world_close_mtx_);
			delayed_world_close_entries_.clear();
		}

		{
			std::lock_guard lk(world_session_mtx_);
			authed_sessions_by_sid_.clear();
			authed_sid_by_char_id_.clear();
			authed_sid_by_account_id_.clear();
		}

		account_line_.host.Stop();
		world_account_handler_.reset();

		{
			std::lock_guard lk(service_line_mtx_);
			zone_routes_by_sid_.clear();
		}
		account_ready_.store(false, std::memory_order_release);
		account_sid_.store(0, std::memory_order_relaxed);
		account_serial_.store(0, std::memory_order_relaxed);

		spdlog::info("WorldServer released.");
	}

	bool WorldRuntime::TryReserveDelayedWorldClose_(
		std::uint32_t sid,
		std::uint32_t serial) noexcept
	{
		if (sid == 0 || serial == 0) {
			return false;
		}

		std::lock_guard lk(delayed_world_close_mtx_);

		const DelayedCloseKey key{ sid, serial };
		const auto [it, inserted] =
			delayed_world_close_entries_.emplace(key, DelayedCloseEntry{});
		return inserted;
	}

	bool WorldRuntime::ArmReservedDelayedWorldClose_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::chrono::milliseconds delay,
		std::uint64_t trace_id,
		std::uint64_t char_id)
	{
		if (sid == 0 || serial == 0) {
			return false;
		}

		const DelayedCloseKey key{ sid, serial };
		auto timer = std::make_shared<boost::asio::steady_timer>(io_);
		timer->expires_after(delay);

		{
			std::lock_guard lk(delayed_world_close_mtx_);

			auto it = delayed_world_close_entries_.find(key);
			if (it == delayed_world_close_entries_.end()) {
				return false;
			}

			if (it->second.armed) {
				return false;
			}

			it->second.timer = timer;
			it->second.armed = true;
			it->second.log_ctx.trace_id = trace_id;
			it->second.log_ctx.char_id = char_id;
			it->second.log_ctx.sid = sid;
			it->second.log_ctx.serial = serial;
		}

		timer->async_wait(
			[this, sid, serial, key, timer](const boost::system::error_code& ec) {
			DelayedCloseEntry fired_entry{};

			{
				std::lock_guard lk(delayed_world_close_mtx_);
				auto it = delayed_world_close_entries_.find(key);
				if (it != delayed_world_close_entries_.end() && it->second.timer == timer) {
					fired_entry = it->second;
					delayed_world_close_entries_.erase(it);
				}
			}

			if (ec == boost::asio::error::operation_aborted) {
				return;
			}

			if (ec) {
				if (fired_entry.log_ctx.trace_id != 0) {
					spdlog::warn(
						"[dup_login trace={}] delayed close timer failed. char_id={} sid={} serial={} ec={}",
						fired_entry.log_ctx.trace_id,
						fired_entry.log_ctx.char_id,
						sid,
						serial,
						ec.message());
				}
				else {
					spdlog::warn(
						"[session_close] delayed close timer failed. char_id={} sid={} serial={} ec={}",
						fired_entry.log_ctx.char_id,
						sid,
						serial,
						ec.message());
				}
				return;
			}

			auto& line = lines_.entry(svr::WorldLineId::World);
			auto world_handler = line.host.handler();
			auto world_server = line.host.server();
			if (!world_server || !world_handler) {
				LogSessionCloseEvent_(
					spdlog::level::warn,
					"delayed close skipped. world server or handler null",
					fired_entry.log_ctx);
				return;
			}

			LogSessionCloseEvent_(
				spdlog::level::info,
				"delayed close fired",
				fired_entry.log_ctx);

			world_handler->Close(
				static_cast<std::uint32_t>(svr::WorldLineId::World),
				sid,
				serial);
		});

		return true;
	}

	bool WorldRuntime::UpdateReservedDelayedWorldCloseContext_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t trace_id,
		std::uint64_t char_id) noexcept
	{
		if (sid == 0 || serial == 0) {
			return false;
		}

		std::lock_guard lk(delayed_world_close_mtx_);

		const DelayedCloseKey key{ sid, serial };
		auto it = delayed_world_close_entries_.find(key);
		if (it == delayed_world_close_entries_.end()) {
			return false;
		}

		it->second.log_ctx.trace_id = trace_id;
		it->second.log_ctx.char_id = char_id;
		it->second.log_ctx.sid = sid;
		it->second.log_ctx.serial = serial;
		return true;
	}

	bool WorldRuntime::ReleaseDelayedWorldCloseReservation_(
		std::uint32_t sid,
		std::uint32_t serial,
		DelayedCloseEntry* released_entry) noexcept
	{
		if (sid == 0 || serial == 0) {
			return false;
		}

		DelayedCloseEntry entry{};

		{
			std::lock_guard lk(delayed_world_close_mtx_);

			const DelayedCloseKey key{ sid, serial };
			auto it = delayed_world_close_entries_.find(key);
			if (it == delayed_world_close_entries_.end()) {
				return false;
			}

			entry = std::move(it->second);
			delayed_world_close_entries_.erase(it);
		}

		if (released_entry) {
			*released_entry = entry;
		}

		if (entry.timer) {
			entry.timer->cancel();
		}

		return true;
	}

	void WorldRuntime::CancelDelayedWorldClose(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		DelayedCloseEntry released_entry{};
		if (ReleaseDelayedWorldCloseReservation_(sid, serial, &released_entry)) {
			LogSessionCloseEvent_(
				spdlog::level::info,
				"delayed close canceled/released",
				released_entry.log_ctx);
		}
	}

	void WorldRuntime::HandleWorldSessionClosed(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		boost::asio::dispatch(
			duplicate_session_strand_,
			[this, sid, serial]() {
			ProcessWorldSessionClosedOnIo_(sid, serial);
		});
	}

	void WorldRuntime::ProcessDuplicateLoginSessionClosedOnIo_(
		const DelayedCloseEntry& released_entry,
		const ClosedAuthedSessionContext& closed_ctx)
	{
		LogSessionCloseEvent_(
			spdlog::level::info,
			"close post-processing released delayed close entry",
			released_entry.log_ctx);

		const auto log_ctx = MakeSessionCloseLogContext_(
			closed_ctx,
			released_entry.log_ctx.trace_id,
			released_entry.log_ctx.char_id);

		if (!closed_ctx.removed()) {
			const auto current = FindAuthenticatedWorldSessionByCharId_(log_ctx.char_id);
			if (current.has_value()) {
				spdlog::info(
					"[dup_login trace={}] stale close guarded. old_char_id={} old_sid={} old_serial={} close_kind={} authoritative_sid={} authoritative_serial={} authoritative_account_id={}",
					log_ctx.trace_id,
					log_ctx.char_id,
					closed_ctx.sid,
					closed_ctx.serial,
					ToString(closed_ctx.unbind_kind),
					current->sid,
					current->serial,
					current->account_id);
			}
		}

		FinalizeWorldSessionClosedOnIo_(
			"world session closed processed on duplicate-login path",
			closed_ctx,
			log_ctx);
	}

	void WorldRuntime::ProcessNormalSessionClosedOnIo_(
		const ClosedAuthedSessionContext& closed_ctx)
	{
		const auto log_ctx = ResolveWorldSessionCloseLogContext_(
			closed_ctx,
			nullptr);

		FinalizeWorldSessionClosedOnIo_(
			"world close post-processing completed on normal path",
			closed_ctx,
			log_ctx);
	}

	void WorldRuntime::FailPendingEnterWorldConsumeRequest_(
		const PendingEnterWorldConsumeRequest& pending,
		pt_w::EnterWorldResultCode reason,
		std::string_view log_text)
	{
		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			return;
		}

		pt_w::S2C_enter_world_result res{};
		res.ok = 0;
		res.reason = static_cast<std::uint16_t>(reason);
		res.account_id = pending.account_id;
		res.char_id = pending.char_id;

		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));

		spdlog::warn(
			"{} request_id={} sid={} serial={} account_id={} char_id={} token={}",
			log_text,
			pending.request_id,
			pending.sid,
			pending.serial,
			pending.account_id,
			pending.char_id,
			pending.world_token);
	}

	void WorldRuntime::CleanupClosedWorldSessionActors_(
		const ClosedAuthedSessionContext& closed_ctx)
	{
		if (!closed_ctx.removed() || closed_ctx.char_id == 0) {
			return;
		}

		PostActor(
			closed_ctx.char_id,
			[this,
			close_char_id = closed_ctx.char_id,
			close_sid = closed_ctx.sid,
			close_serial = closed_ctx.serial]() {
			auto& a = GetOrCreatePlayerActor(close_char_id);

			if (a.sid == close_sid && a.serial == close_serial) {
				a.unbind_session(close_sid, close_serial);
			}

			if (a.zone_id != 0) {
				const auto zone_id = a.zone_id;
				const auto map_template_id = a.map_template_id;
				const auto map_instance_id = a.map_instance_id;
				SendZonePlayerLeave_(static_cast<std::uint16_t>(zone_id), close_char_id, map_template_id, map_instance_id);
				PostActor(
					svr::MakeZoneActorId(zone_id),
					[this, close_char_id, zone_id]() {
					auto& z = GetOrCreateZoneActor(zone_id);
					z.Leave(close_char_id);
				});
			}
		});
	}

	void WorldRuntime::FinalizeWorldSessionClosedOnIo_(
		std::string_view processed_text,
		const ClosedAuthedSessionContext& closed_ctx,
		const SessionCloseLogContext& log_ctx) const
	{
		LogSessionCloseProcessed_(
			spdlog::level::info,
			processed_text,
			log_ctx,
			closed_ctx.removed());

		LogWorldSessionClosePostState_(
			closed_ctx,
			log_ctx.trace_id);
	}

	void WorldRuntime::ProcessWorldSessionClosedOnIo_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (sid == 0 || serial == 0) {
			return;
		}

		DelayedCloseEntry released_entry{};
		const bool released = ReleaseDelayedWorldCloseReservation_(
			sid,
			serial,
			&released_entry);

		const auto unbind_result =
			UnbindAuthenticatedWorldSessionBySid(sid, serial);

		ClosedAuthedSessionContext closed_ctx =
			MakeClosedAuthedSessionContext_(unbind_result, sid, serial);

		CleanupClosedWorldSessionActors_(closed_ctx);

		if (released && released_entry.log_ctx.trace_id != 0) {
			if (closed_ctx.char_id == 0) {
				closed_ctx.char_id = released_entry.log_ctx.char_id;
			}

			ProcessDuplicateLoginSessionClosedOnIo_(
				released_entry,
				closed_ctx);
			return;
		}

		ProcessNormalSessionClosedOnIo_(closed_ctx);
	}

	void WorldRuntime::LogWorldSessionClosePostState_(
		const ClosedAuthedSessionContext& closed_ctx,
		std::uint64_t trace_id) const
	{
		if (trace_id != 0) {
			spdlog::info(
				"[dup_login trace={}] duplicate-login close post-state kind={} account_id={} char_id={} sid={} serial={}",
				trace_id,
				ToString(closed_ctx.unbind_kind),
				closed_ctx.account_id,
				closed_ctx.char_id,
				closed_ctx.sid,
				closed_ctx.serial);
			return;
		}

		spdlog::info(
			"[session_close] close post-state kind={} account_id={} char_id={} sid={} serial={}",
			ToString(closed_ctx.unbind_kind),
			closed_ctx.account_id,
			closed_ctx.char_id,
			closed_ctx.sid,
			closed_ctx.serial);
	}

	void WorldRuntime::CancelDelayedWorldCloseTimers_() noexcept
	{
		std::unordered_map<
			DelayedCloseKey,
			DelayedCloseEntry,
			DelayedCloseKeyHash> entries;

		{
			std::lock_guard lk(delayed_world_close_mtx_);
			entries.swap(delayed_world_close_entries_);
		}

		for (auto& [key, entry] : entries) {
			if (!entry.timer) {
				continue;
			}

			LogSessionCloseEvent_(
				spdlog::level::info,
				"cancel delayed close timer on shutdown",
				entry.log_ctx);

			entry.timer->cancel();
		}
	}

	void WorldRuntime::ProcessDuplicateWorldSessionKickOnIo_(
		DuplicateLoginLogContext ctx)
	{
		if (ctx.char_id == 0 || ctx.sid == 0 || ctx.serial == 0) {
			return;
		}

		auto& line = lines_.entry(svr::WorldLineId::World);
		auto world_handler = line.host.handler();
		auto world_server = line.host.server();
		if (!world_server || !world_handler) {
			LogDuplicateWorldSessionEvent_(
				spdlog::level::warn,
				"world server or handler null",
				ctx);
			return;
		}

		if (!TryBeginDuplicateWorldSessionKickClose_(ctx)) {
			return;
		}

		const bool send_ok = SendDuplicateWorldSessionKick_(ctx, *world_handler);

		if (send_ok) {
			LogDuplicateWorldSessionEvent_(
				spdlog::level::info,
				"kick packet send accepted",
				ctx);

			const bool armed = ArmReservedDelayedWorldClose_(
				ctx.sid,
				ctx.serial,
				kDuplicateKickCloseDelay_,
				ctx.trace_id,
				ctx.char_id);

			if (armed) {
				LogDuplicateWorldSessionCloseDecision_(
					spdlog::level::warn,
					"serialized kick sent -> delayed close old session",
					ctx);

				spdlog::info(
					"[dup_login trace={}] delayed close armed. char_id={} old_sid={} old_serial={} delay_ms={}",
					ctx.trace_id,
					ctx.char_id,
					ctx.sid,
					ctx.serial,
					kDuplicateKickCloseDelay_.count());
			}
			else {
				CloseDuplicateWorldSessionImmediately_(
					ctx,
					"serialized kick sent but delayed close arm failed",
					*world_handler);
			}
		}
		else {
			CloseDuplicateWorldSessionImmediately_(
				ctx,
				"serialized kick send failed",
				*world_handler);
		}
	}

	void WorldRuntime::PostActor(std::uint64_t actor_id, std::function<void()> fn)
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

	svr::PlayerActor& WorldRuntime::GetOrCreatePlayerActor(std::uint64_t char_id)
	{
		auto& base = actors_.get_or_create_local(char_id, &MakeServerActor_);
		return static_cast<svr::PlayerActor&>(base);
	}

	svr::WorldActor& WorldRuntime::GetOrCreateWorldActor()
	{
		auto& base = actors_.get_or_create_local(0, &MakeServerActor_);
		return static_cast<svr::WorldActor&>(base);
	}

	svr::ZoneActor& WorldRuntime::GetOrCreateZoneActor(std::uint32_t zone_id)
	{
		const std::uint64_t zid = svr::MakeZoneActorId(zone_id);
		auto& base = actors_.get_or_create_local(zid, &MakeServerActor_);
		return static_cast<svr::ZoneActor&>(base);
	}

	void WorldRuntime::EraseActor(std::uint64_t actor_id)
	{
		actors_.erase_actor(actor_id);
	}

	std::uint64_t WorldRuntime::FindCharIdBySession(std::uint32_t sid) const
	{
		if (sid == 0) {
			return 0;
		}

		std::lock_guard lk(world_session_mtx_);

		auto it = authed_sessions_by_sid_.find(sid);
		if (it == authed_sessions_by_sid_.end()) {
			return 0;
		}

		return it->second.char_id;
	}

	BindAuthedWorldSessionResult WorldRuntime::BindAuthenticatedWorldSessionForLogin(
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint16_t kick_reason)
	{
		// 정책:
		// - account_id 당 인증된 활성 월드 세션은 최대 1개만 허용한다.
		// - 새 월드 입장 바인딩이 성공하면 동일 char_id 또는 동일 account_id 에 묶여 있던 기존 세션은 강제 종료한다.
		// - 새 세션이 최종 권한(authoritative) 세션이 된다.

		BindAuthedWorldSessionResult result{};
		result.current_session = WorldAuthedSession{
			account_id,
			char_id,
			sid,
			serial
		};

		if (account_id == 0 || char_id == 0 || sid == 0 || serial == 0) {
			result.kind = BindAuthedWorldSessionResultKind::InvalidInput;
			result.duplicate_cause = DuplicateSessionCause::None;
			spdlog::warn(
				"BindAuthenticatedWorldSessionForLogin invalid input. account_id={} char_id={} sid={} serial={}",
				account_id,
				char_id,
				sid,
				serial);
			return result;
		}

		{
			std::lock_guard lk(world_session_mtx_);

			auto erase_authed_sid_nolock =
				[this](std::uint32_t victim_sid, WorldAuthedSession* removed_session = nullptr)
			{
				auto victim_it = authed_sessions_by_sid_.find(victim_sid);
				if (victim_it == authed_sessions_by_sid_.end()) {
					return false;
				}

				const auto victim = victim_it->second;

				if (removed_session) {
					*removed_session = victim;
				}

				if (victim.char_id != 0) {
					auto char_it = authed_sid_by_char_id_.find(victim.char_id);
					if (char_it != authed_sid_by_char_id_.end() && char_it->second == victim_sid) {
						authed_sid_by_char_id_.erase(char_it);
					}
				}

				if (victim.account_id != 0) {
					auto account_it = authed_sid_by_account_id_.find(victim.account_id);
					if (account_it != authed_sid_by_account_id_.end() && account_it->second == victim_sid) {
						authed_sid_by_account_id_.erase(account_it);
					}
				}

				authed_sessions_by_sid_.erase(victim_it);
				return true;
			};

			auto same_sid_it = authed_sessions_by_sid_.find(sid);
			if (same_sid_it != authed_sessions_by_sid_.end()) {
				const auto& existing = same_sid_it->second;
				if (existing.serial == serial &&
					existing.account_id == account_id &&
					existing.char_id == char_id)
				{
					result.kind = BindAuthedWorldSessionResultKind::AlreadyBoundSameSession;
					result.duplicate_cause = DuplicateSessionCause::None;
				}
				else {
					erase_authed_sid_nolock(sid);
				}
			}

			if (result.kind != BindAuthedWorldSessionResultKind::AlreadyBoundSameSession) {
				auto char_sid_it = authed_sid_by_char_id_.find(char_id);
				if (char_sid_it != authed_sid_by_char_id_.end()) {
					auto old_it = authed_sessions_by_sid_.find(char_sid_it->second);
					if (old_it != authed_sessions_by_sid_.end()) {
						result.old_char_session = old_it->second;
					}
				}

				auto account_sid_it = authed_sid_by_account_id_.find(account_id);
				if (account_sid_it != authed_sid_by_account_id_.end()) {
					auto old_it = authed_sessions_by_sid_.find(account_sid_it->second);
					if (old_it != authed_sessions_by_sid_.end()) {
						result.old_account_session = old_it->second;
					}
				}

				if (result.has_old_char_session()) {
					erase_authed_sid_nolock(result.old_char_session.sid);
				}

				if (result.has_old_account_session() &&
					result.old_account_session.sid != result.old_char_session.sid)
				{
					erase_authed_sid_nolock(result.old_account_session.sid);
				}

				authed_sessions_by_sid_[sid] = result.current_session;
				authed_sid_by_char_id_[char_id] = sid;
				authed_sid_by_account_id_[account_id] = sid;

				if (result.has_old_char_session() && result.has_old_account_session()) {
					result.kind = BindAuthedWorldSessionResultKind::ReplacedBoth;
					result.duplicate_cause = DuplicateSessionCause::DuplicateCharAndAccountSession;
				}
				else if (result.has_old_char_session()) {
					result.kind = BindAuthedWorldSessionResultKind::ReplacedCharSession;
					result.duplicate_cause = DuplicateSessionCause::DuplicateCharSession;
				}
				else if (result.has_old_account_session()) {
					result.kind = BindAuthedWorldSessionResultKind::ReplacedAccountSession;
					result.duplicate_cause = DuplicateSessionCause::DuplicateAccountSession;
				}
				else {
					result.kind = BindAuthedWorldSessionResultKind::Inserted;
					result.duplicate_cause = DuplicateSessionCause::None;
				}
			}
		}

		if (result.has_old_char_session()) {
			EnqueueDuplicateAuthedSessionCloseIfNeeded_(
				result.old_char_session,
				account_id,
				char_id,
				sid,
				serial,
				kick_reason,
				(result.has_old_account_session()
					? DuplicateSessionCause::DuplicateCharAndAccountSession
					: DuplicateSessionCause::DuplicateCharSession),
				(result.has_old_account_session()
					? SessionKickStatCategory::DuplicateBoth
					: SessionKickStatCategory::DuplicateChar));
		}

		if (result.has_old_account_session()) {
			const bool same_old_session =
				result.old_account_session.sid == result.old_char_session.sid &&
				result.old_account_session.serial == result.old_char_session.serial;

			if (!same_old_session) {
				EnqueueDuplicateAuthedSessionCloseIfNeeded_(
					result.old_account_session,
					account_id,
					char_id,
					sid,
					serial,
					kick_reason,
					(result.has_old_char_session()
						? DuplicateSessionCause::DuplicateCharAndAccountSession
						: DuplicateSessionCause::DuplicateAccountSession),
					(result.has_old_char_session()
						? SessionKickStatCategory::DuplicateBoth
						: SessionKickStatCategory::DuplicateAccount));
			}
			else {
				spdlog::info(
					"BindAuthenticatedWorldSessionForLogin detected shared old char/account session. deduplicated duplicate close enqueue. account_id={} char_id={} old_sid={} old_serial={} new_sid={} new_serial={} stat_category={}",
					account_id,
					char_id,
					result.old_account_session.sid,
					result.old_account_session.serial,
					sid,
					serial,
					ToString(SessionKickStatCategory::DuplicateDeduplicatedSameSession));
			}
		}

		spdlog::info(
			"World authenticated session bound. bind_kind={} duplicate_cause={} account_id={} char_id={} sid={} serial={} old_char_sid={} old_char_serial={} old_account_sid={} old_account_serial={}",
			ToString(result.kind),
			ToString(result.duplicate_cause),
			account_id,
			char_id,
			sid,
			serial,
			result.old_char_session.sid,
			result.old_char_session.serial,
			result.old_account_session.sid,
			result.old_account_session.serial);

		return result;
	}

	UnbindAuthedWorldSessionResult WorldRuntime::UnbindAuthenticatedWorldSessionBySid(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		UnbindAuthedWorldSessionResult result{};

		if (sid == 0 || serial == 0) {
			result.kind = UnbindAuthedWorldSessionResultKind::InvalidInput;
			spdlog::warn(
				"UnbindAuthenticatedWorldSessionBySid invalid input. sid={} serial={}",
				sid, serial);
			return result;
		}

		{
			std::lock_guard lk(world_session_mtx_);

			auto it = authed_sessions_by_sid_.find(sid);
			if (it == authed_sessions_by_sid_.end()) {
				result.kind = UnbindAuthedWorldSessionResultKind::NotFoundBySid;
				spdlog::info(
					"World authenticated session unbind skipped. kind={} sid={} serial={}",
					ToString(result.kind),
					sid,
					serial);
				return result;
			}

			result.session = it->second;

			if (result.session.serial != serial) {
				result.kind = UnbindAuthedWorldSessionResultKind::SerialMismatch;
				spdlog::info(
					"World authenticated session unbind skipped. kind={} sid={} serial={} bound_account_id={} bound_char_id={} bound_serial={}",
					ToString(result.kind),
					sid,
					serial,
					result.session.account_id,
					result.session.char_id,
					result.session.serial);
				return result;
			}

			if (result.session.char_id != 0) {
				auto char_it = authed_sid_by_char_id_.find(result.session.char_id);
				if (char_it != authed_sid_by_char_id_.end() && char_it->second == sid) {
					authed_sid_by_char_id_.erase(char_it);
				}
			}

			if (result.session.account_id != 0) {
				auto account_it = authed_sid_by_account_id_.find(result.session.account_id);
				if (account_it != authed_sid_by_account_id_.end() && account_it->second == sid) {
					authed_sid_by_account_id_.erase(account_it);
				}
			}

			authed_sessions_by_sid_.erase(it);
			result.kind = UnbindAuthedWorldSessionResultKind::Removed;
		}

		spdlog::info(
			"World authenticated session unbound. kind={} account_id={} char_id={} sid={} serial={}",
			ToString(result.kind),
			result.session.account_id,
			result.session.char_id,
			sid,
			serial);

		return result;
	}

	void WorldRuntime::Post(std::function<void()> fn)
	{
		PostActor(0, std::move(fn));
	}

	void WorldRuntime::PostDqsResult(svr::dqs_result::Result r)
	{
		// ✅ DQS 결과도 Actor로 라우팅
		std::uint64_t actor_id = 0;
		std::visit([this, &actor_id](auto&& rr) {
			using T = std::decay_t<decltype(rr)>;
			if constexpr (std::is_same_v<T, svr::dqs_result::OpenWorldNoticeResult>) {
				if (auto* h = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>()) actor_id = h->GetActorIdBySession(rr.sid);
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

	void WorldRuntime::HandleDqsResult_(const svr::dqs_result::Result& r)
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
				if (!lines_.host(svr::WorldLineId::World).started()) return;

				proto::S2C_open_world_success res{};
				res.ok = (proto::u32)rr.ok;

				auto h = proto::make_header(
					(std::uint16_t)proto::S2CMsg::open_world_success,
					(std::uint16_t)sizeof(res));

				// serial 검증은 TcpServer::send 내부의 세션(serial) 체크에 맡긴다.
				lines_.host(svr::WorldLineId::World).server()->send(rr.sid, rr.serial, h, reinterpret_cast<const char*>(&res));
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

	void WorldRuntime::EnqueueFlushDirtyWorld_(std::uint32_t world_code, std::uint32_t batch)
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

	void WorldRuntime::CloseWorldServer(std::uint32_t world_socket_index) {
		if (!lines_.host(svr::WorldLineId::World).server()) return;
		lines_.host(svr::WorldLineId::World).server()->close(world_socket_index);

	}

	void WorldRuntime::RegisterControlLine(
		std::uint32_t sid, std::uint32_t serial,
		std::uint32_t server_id, std::string_view server_name,
		std::uint16_t listen_port)
	{
		std::lock_guard lk(service_line_mtx_);

		control_line_state_.registered = true;
		control_line_state_.sid = sid;
		control_line_state_.serial = serial;
		control_line_state_.server_id = server_id;
		control_line_state_.server_name = std::string(server_name);
		control_line_state_.listen_port = listen_port;

		spdlog::info(
			"WorldRuntime registered control line. sid={} serial={} server_id={} name={} listen_port={}",
			sid, serial, server_id, server_name, listen_port);
	}

	void WorldRuntime::UnregisterControlLine(std::uint32_t sid, std::uint32_t serial)
	{
		std::lock_guard lk(service_line_mtx_);

		if (!control_line_state_.registered) {
			return;
		}
		if (control_line_state_.sid != sid || control_line_state_.serial != serial) {
			return;
		}

		spdlog::info(
			"WorldRuntime unregistered control line. sid={} serial={} server_id={} name={}",
			control_line_state_.sid,
			control_line_state_.serial,
			control_line_state_.server_id,
			control_line_state_.server_name);

		control_line_state_ = {};
	}


	bool WorldRuntime::PushDQSData(std::uint8_t process_code, std::uint8_t qry_case, const char* data, int size) {
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

	std::uint32_t WorldRuntime::RouteShard_(std::uint8_t qry_case, const char* data, int size) const
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

	bool WorldRuntime::LoadIniFile()
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

	bool WorldRuntime::DatabaseInit()
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

	bool WorldRuntime::EnsureAccountHandler_()
	{
		if (world_account_handler_) {
			return true;
		}

		world_account_handler_ = std::make_shared<WorldAccountHandler>(
			*this,
			[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id,
				std::uint16_t world_id, std::uint16_t channel_id,
				std::uint16_t active_zone_count, std::uint16_t load_score, std::uint32_t flags,
				std::string_view server_name, std::string_view public_host, std::uint16_t public_port) {
			MarkAccountRegistered_(sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags, server_name, public_host, public_port);
		},
			[this](std::uint32_t sid, std::uint32_t serial) {
			MarkAccountDisconnected_(sid, serial);
		});

		if (!world_account_handler_) {
			spdlog::error("WorldRuntime failed to create world_account_handler_");
			return false;
		}

		return true;
	}

	bool WorldRuntime::NetworkInit()
	{
		auto dispatch = [this](std::uint64_t actor_id, std::function<void()> fn) {
			PostActor(actor_id, std::move(fn));
		};

		InitHostedLineDescriptors_();

		if (!dc::StartHostedLine(
			lines_.entry(svr::WorldLineId::World),
			io_,
			std::make_shared<WorldHandler>(*this),
			dispatch)) {
			return false;
		}

		if (!dc::StartHostedLine(
			lines_.entry(svr::WorldLineId::Zone),
			io_,
			std::make_shared<WorldZoneHandler>(
				[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id,
					std::uint16_t zone_id, std::uint16_t world_id, std::uint16_t channel_id,
					std::uint16_t map_instance_capacity, std::uint16_t active_map_instance_count,
					std::uint16_t active_player_count, std::uint16_t load_score, std::uint32_t flags, std::string_view server_name) {
			RegisterZoneRoute(sid, serial, server_id, zone_id, world_id, channel_id, map_instance_capacity, active_map_instance_count, active_player_count, load_score, flags, server_name);
		},
				[this](std::uint32_t sid, std::uint32_t serial, std::uint32_t server_id,
					std::uint16_t zone_id, std::uint16_t world_id, std::uint16_t channel_id,
					std::uint16_t map_instance_capacity, std::uint16_t active_map_instance_count,
					std::uint16_t active_player_count, std::uint16_t load_score, std::uint32_t flags) {
			OnZoneRouteHeartbeat(sid, serial, server_id, zone_id, world_id, channel_id, map_instance_capacity, active_map_instance_count, active_player_count, load_score, flags);
		},
				[this](std::uint32_t sid, std::uint32_t serial) {
			UnregisterZoneRoute(sid, serial);
		},
				[this](std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldMapAssignResponse& res) {
			OnZoneMapAssignResponse(sid, serial, res);
		}),
			dispatch)) {
			return false;
		}

		if (!dc::StartHostedLine(
			lines_.entry(svr::WorldLineId::Control),
			io_,
			std::make_shared<WorldControlHandler>(
				[this](std::uint32_t sid, std::uint32_t serial,
					std::uint32_t server_id, std::string_view server_name,
					std::uint16_t listen_port) {
			RegisterControlLine(sid, serial, server_id, server_name, listen_port);
		},
				[this](std::uint32_t sid, std::uint32_t serial) {
			UnregisterControlLine(sid, serial);
		}),
			dispatch)) {
			return false;
		}

		if (!EnsureAccountHandler_()) {
			return false;
		}

		world_account_handler_->SetServerIdentity(
			10,
			1,
			1,
			"world",
			"127.0.0.1",
			port_world_,
			0,
			0,
			pt_aw::k_world_flag_accepting_players | pt_aw::k_world_flag_visible);

		dc::InitOutboundLineEntry(
			account_line_,
			100,
			"world-account",
			account_host_,
			account_port_,
			true,
			1000,
			10000);

		if (!dc::StartOutboundLine(
			account_line_,
			io_,
			world_account_handler_,
			[this](std::uint64_t actor_id, std::function<void()> fn) {
			PostActor(actor_id, std::move(fn));
		}))
		{
			spdlog::error(
				"WorldRuntime failed to start outbound account line. remote={}:{}",
				account_host_,
				account_port_);
			return false;
		}

		spdlog::info("NetworkInit: world={} zone={} control={} account_remote={}:{}",
			lines_.host(svr::WorldLineId::World).port(),
			lines_.host(svr::WorldLineId::Zone).port(),
			lines_.host(svr::WorldLineId::Control).port(),
			account_host_,
			account_port_);

		return true;
	}

	bool WorldRuntime::InitDQS()
	{
		std::lock_guard lk(dqs_mtx_);
		dqs_slots_.assign(MAX_DB_SYC_DATA_NUM, {});
		dqs_empty_.clear();
		for (std::uint32_t i = 0; i < MAX_DB_SYC_DATA_NUM; ++i)
			dqs_empty_.push_back(i);
		return true;
	}

	void WorldRuntime::OnDQSRunOne(std::uint32_t slot_index)
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

	bool WorldRuntime::InitRedis()
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

	void WorldRuntime::ScheduleFlush_()
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

	void WorldRuntime::EnqueueFlushDirty_(bool immediate)
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

	void WorldRuntime::CacheCharacterState(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob)
	{
		if (!redis_cache_) return;
		const int ttl = char_ttl_sec_;

		try { redis_cache_->upsert_character(world_code, char_id, blob, ttl); }
		catch (const std::exception& e) { spdlog::error("CacheCharacterState failed: {}", e.what()); }
	}

	std::optional<std::string> WorldRuntime::TryLoadCharacterState(std::uint32_t world_code, std::uint64_t char_id)
	{
		if (!redis_cache_) return std::nullopt;
		try { return redis_cache_->get_character_blob(world_code, char_id); }
		catch (const std::exception& e)
		{
			spdlog::error("TryLoadCharacterState failed: {}", e.what());
			return std::nullopt;
		}
	}

	void WorldRuntime::RequestFlushCharacter(std::uint32_t world_code, std::uint64_t char_id)
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
		svr::g_s2c_move_pkts_sent.store(0, std::memory_order_relaxed);
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
		bench_base_c2s_move_rx_ = svr::g_c2s_bench_move_rx.load(std::memory_order_relaxed);
		bench_base_s2c_ack_tx_ = svr::g_s2c_bench_ack_tx.load(std::memory_order_relaxed);
		bench_base_s2c_ack_drop_ = svr::g_s2c_bench_ack_drop.load(std::memory_order_relaxed);
		bench_base_s2c_move_pkts_ = svr::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
		bench_base_s2c_move_items_ = svr::g_s2c_move_items_sent.load(std::memory_order_relaxed);
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

		const auto cur_c2s = svr::g_c2s_bench_move_rx.load(std::memory_order_relaxed);
		const auto cur_ack = svr::g_s2c_bench_ack_tx.load(std::memory_order_relaxed);
		const auto cur_ack_drop = svr::g_s2c_bench_ack_drop.load(std::memory_order_relaxed);
		const auto cur_move_pkts = svr::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
		const auto cur_move_items = svr::g_s2c_move_items_sent.load(std::memory_order_relaxed);

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

	void ServerProgramExit(const char* call_site, bool /*save*/) {
		spdlog::warn("ServerProgramExit called from '{}'. stopping...", call_site ? call_site : "(null)");
		g_Main.ReleaseMainThread();
	}

	std::uint64_t WorldRuntime::MakeMapAssignmentKey_(std::uint32_t map_template_id, std::uint32_t instance_id) const noexcept
	{
		return (static_cast<std::uint64_t>(map_template_id) << 32) | static_cast<std::uint64_t>(instance_id);
	}

	void WorldRuntime::RegisterZoneRoute(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneServerHello& req)
	{
		RegisterZoneRoute(sid, serial, req.server_id, req.zone_id, req.world_id, req.channel_id, req.map_instance_capacity, req.active_map_instance_count, req.active_player_count, req.load_score, req.flags, req.server_name);
	}

	void WorldRuntime::OnZoneRouteHeartbeat(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneServerRouteHeartbeat& req)
	{
		OnZoneRouteHeartbeat(sid, serial, req.server_id, req.zone_id, req.world_id, req.channel_id, req.map_instance_capacity, req.active_map_instance_count, req.active_player_count, req.load_score, req.flags);
	}

	void WorldRuntime::OnZoneMapAssignResponse(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldMapAssignResponse& res)
	{
		auto it = pending_zone_assign_requests_.find(res.request_id);
		if (it == pending_zone_assign_requests_.end()) {
			return;
		}
		if (it->second.target_sid != sid || it->second.target_serial != serial) {
			return;
		}
		if (res.result_code == 0) {
			map_assignments_[MakeMapAssignmentKey_(res.map_template_id, res.instance_id)] =
				MapAssignmentEntry{ res.zone_id, res.map_template_id, res.instance_id };
		}
		pending_zone_assign_requests_.erase(it);
	}


	std::optional<WorldRuntime::ZoneRouteInfo> WorldRuntime::TrySelectZoneRoute_(bool dungeon_instance) const
	{
		std::optional<ZoneRouteInfo> best;
		for (const auto& [sid, route] : zone_routes_by_sid_) {
			if (route.serial == 0) {
				continue;
			}

			ZoneRouteInfo current{};
			current.sid = route.sid;
			current.serial = route.serial;
			current.zone_server_id = route.server_id;
			current.zone_id = route.zone_id;
			current.active_map_instance_count = route.active_map_instance_count;
			current.load_score = route.load_score;
			current.flags = route.flags;
			current.last_heartbeat_at = route.last_heartbeat_at;

			if (!best.has_value()) {
				best = current;
				continue;
			}
			if (current.load_score < best->load_score) {
				best = current;
				continue;
			}
			if (current.load_score == best->load_score && dungeon_instance &&
				current.active_map_instance_count < best->active_map_instance_count) {
				best = current;
				continue;
			}
			if (current.load_score == best->load_score &&
				current.active_player_count < best->active_player_count) {
				best = current;
 			}
		}
		return best;
	}

	std::optional<WorldRuntime::ZoneRouteInfo> WorldRuntime::FindZoneRouteByZoneId_(std::uint16_t zone_id) const
	{
		std::lock_guard lk(service_line_mtx_);
		for (const auto& [_, route] : zone_routes_by_sid_) {
			if (route.registered && route.zone_id == zone_id) {
				ZoneRouteInfo current{};
				current.sid = route.sid;
				current.serial = route.serial;
				current.zone_server_id = route.server_id;
				current.zone_id = route.zone_id;
				current.active_map_instance_count = route.active_map_instance_count;
				current.active_player_count = route.active_player_count;
				current.load_score = route.load_score;
				current.flags = route.flags;
				current.last_heartbeat_at = route.last_heartbeat_at;
				return current;
			}
		}
		return std::nullopt;
	}

	bool WorldRuntime::SendZonePlayerEnter_(std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id)
	{
		auto route = FindZoneRouteByZoneId_(zone_id);
		if (!route.has_value()) {
			return false;
		}
		auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>();
		if (!handler) {
			return false;
		}
		return handler->SendPlayerEnter(0, route->sid, route->serial, char_id, map_template_id, instance_id, zone_id);
	}

	bool WorldRuntime::SendZonePlayerLeave_(std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id)
	{
		auto route = FindZoneRouteByZoneId_(zone_id);
		if (!route.has_value()) {
			return false;
		}
		auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>();
		if (!handler) {
			return false;
		}
		return handler->SendPlayerLeave(0, route->sid, route->serial, char_id, map_template_id, instance_id, zone_id);
 	}

	svr::AssignMapInstanceResult WorldRuntime::AssignMapInstance(
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		bool create_if_missing,
		bool dungeon_instance)
	{
		AssignMapInstanceResult result{};
		result.map_template_id = map_template_id;
		result.instance_id = instance_id;

		const auto key = MakeMapAssignmentKey_(map_template_id, instance_id);
		if (auto it = map_assignments_.find(key); it != map_assignments_.end()) {
			result.kind = AssignMapInstanceResultKind::Ok;
			result.zone_id = it->second.zone_id;
			return result;
		}

		auto zone = TrySelectZoneRoute_(dungeon_instance);
		if (!zone.has_value()) {
			result.kind = AssignMapInstanceResultKind::NoZoneAvailable;
			return result;
		}

		auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>();
		if (!handler) {
			result.kind = AssignMapInstanceResultKind::RequestSendFailed;
			return result;
		}

		const std::uint64_t request_id = next_zone_assign_request_id_++;
		PendingZoneAssignRequest pending{};
		pending.request_id = request_id;
		pending.target_sid = zone->sid;
		pending.target_serial = zone->serial;
		pending.map_template_id = map_template_id;
		pending.instance_id = instance_id;
		pending.issued_at = std::chrono::steady_clock::now();
		pending_zone_assign_requests_[request_id] = pending;

		if (!handler->SendMapAssignRequest(0, zone->sid, zone->serial, request_id, map_template_id, instance_id, create_if_missing, dungeon_instance)) {
			pending_zone_assign_requests_.erase(request_id);
			result.kind = AssignMapInstanceResultKind::RequestSendFailed;
			return result;
		}

		result.kind = AssignMapInstanceResultKind::Ok;
		result.zone_id = zone->zone_id;
		map_assignments_[key] = MapAssignmentEntry{ zone->zone_id, map_template_id, instance_id };
		return result;
	}

	void WorldRuntime::OnMapAssignRequest(
		std::uint32_t /*sid*/,
		std::uint32_t /*serial*/,
		const pt_wz::WorldZoneMapAssignRequest& /*req*/)
	{
		// WorldCoordinator는 map assign request의 발신자다.
		// 이 경로는 현재 구조에서 수신측으로 사용하지 않는다.
	}

	void WorldRuntime::OnMainLoopTick(std::chrono::steady_clock::time_point now) {
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

		if (account_ready_.load(std::memory_order_acquire) && now >= next_account_route_heartbeat_tp_) {
			next_account_route_heartbeat_tp_ = now + std::chrono::seconds(3);
			SendAccountRouteHeartbeat_();
		}

		ExpireStaleZoneRoutes_(now);

		if (now >= next_stat_tp_) {
			next_stat_tp_ = now + std::chrono::seconds(1);

			const auto cur_pkts = svr::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
			const auto cur_items = svr::g_s2c_move_items_sent.load(std::memory_order_relaxed);
			const auto d_pkts = cur_pkts - last_move_pkts_;
			const auto d_items = cur_items - last_move_items_;
			last_move_pkts_ = cur_pkts;
			last_move_items_ = cur_items;

			if (d_pkts > 0 || d_items > 0) {
				spdlog::info("[netstats] s2c_move pkts/s={} items/s={}", d_pkts, d_items);
			}

			const auto& world_line = lines_.host(svr::WorldLineId::World);
			const auto& zone_line = lines_.host(svr::WorldLineId::Zone);
			const auto& control_line = lines_.host(svr::WorldLineId::Control);

			spdlog::info(
				"[linestats] world(cur={}, peak={}, open={}, close={}) "
				"zone(cur={}, peak={}, open={}, close={}) "
				"control(cur={}, peak={}, open={}, close={})",
				world_line.stats().current_sessions.load(std::memory_order_relaxed),
				world_line.stats().peak_sessions.load(std::memory_order_relaxed),
				world_line.stats().session_open_count.load(std::memory_order_relaxed),
				world_line.stats().session_close_count.load(std::memory_order_relaxed),

				zone_line.stats().current_sessions.load(std::memory_order_relaxed),
				zone_line.stats().peak_sessions.load(std::memory_order_relaxed),
				zone_line.stats().session_open_count.load(std::memory_order_relaxed),
				zone_line.stats().session_close_count.load(std::memory_order_relaxed),

				control_line.stats().current_sessions.load(std::memory_order_relaxed),
				control_line.stats().peak_sessions.load(std::memory_order_relaxed),
				control_line.stats().session_open_count.load(std::memory_order_relaxed),
				control_line.stats().session_close_count.load(std::memory_order_relaxed));
		}
	}

	bool WorldRuntime::RequestConsumeWorldAuthTicket(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view token)
	{
		if (!account_ready_.load(std::memory_order_acquire)) {
			spdlog::warn("RequestConsumeWorldAuthTicket skipped: account line not ready. sid={} serial={} account_id={} char_id={}",
				sid, serial, account_id, char_id);
			return false;
		}

		if (!world_account_handler_) {
			spdlog::warn("RequestConsumeWorldAuthTicket skipped: world_account_handler_ is null. sid={} serial={} account_id={} char_id={}",
				sid, serial, account_id, char_id);
			return false;
		}

		const auto request_id = next_world_auth_consume_request_id_.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			pending_enter_world_consume_[request_id] = PendingEnterWorldConsumeRequest{
				request_id,
				sid,
				serial,
				account_id,
				char_id,
				std::string(login_session),
				std::string(token),
				std::chrono::steady_clock::now()
			};
		}

		const bool sent = world_account_handler_->SendWorldAuthTicketConsumeRequest(
			100,
			account_sid_.load(std::memory_order_relaxed),
			account_serial_.load(std::memory_order_relaxed),
			request_id,
			account_id,
			char_id,
			login_session,
			token);

		if (!sent) {
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			pending_enter_world_consume_.erase(request_id);
		}

		return sent;
	}

	void WorldRuntime::OnWorldAuthTicketConsumeResponse(
		std::uint64_t request_id,
		ConsumePendingWorldAuthTicketResultKind result_kind,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		PendingEnterWorldConsumeRequest pending{};
		{
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			const auto it = pending_enter_world_consume_.find(request_id);
			if (it == pending_enter_world_consume_.end()) {
				spdlog::warn("OnWorldAuthTicketConsumeResponse stale response ignored. request_id={} account_id={} char_id={}",
					request_id,
					account_id,
					char_id);
				return;
			}
			pending = std::move(it->second);
			pending_enter_world_consume_.erase(it);
		}

		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			return;
		}

		pt_w::S2C_enter_world_result res{};
		res.account_id = pending.account_id;
		res.char_id = pending.char_id;

		if (pending.account_id != account_id ||
			pending.char_id != char_id ||
			pending.login_session != login_session ||
			pending.world_token != world_token)
		{
			spdlog::warn(
				"OnWorldAuthTicketConsumeResponse mismatch. request_id={} pending_account_id={} resp_account_id={} pending_char_id={} resp_char_id={}",
				request_id,
				pending.account_id,
				account_id,
				pending.char_id,
				char_id);

			res.ok = 0;
			res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::auth_ticket_mismatch);
			const auto h = proto::make_header(
				static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
				static_cast<std::uint16_t>(sizeof(res)));
			handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));
			return;
		}

		const std::string final_login_session =
			login_session.empty() ? pending.login_session : std::string(login_session);
		const std::string final_world_token =
			world_token.empty() ? pending.world_token : std::string(world_token);

		if (result_kind != ConsumePendingWorldAuthTicketResultKind::Ok) {
			res.ok = 0;
			switch (result_kind) {
			case ConsumePendingWorldAuthTicketResultKind::Expired:
				res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::auth_ticket_expired);
				break;
			case ConsumePendingWorldAuthTicketResultKind::ReplayDetected:
				res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::auth_ticket_replayed);
				break;
			case ConsumePendingWorldAuthTicketResultKind::AccountMismatch:
			case ConsumePendingWorldAuthTicketResultKind::CharMismatch:
			case ConsumePendingWorldAuthTicketResultKind::LoginSessionMismatch:
				res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::auth_ticket_mismatch);
				break;
			case ConsumePendingWorldAuthTicketResultKind::TokenNotFound:
			default:
				res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::auth_ticket_not_found);
				break;
			}

			const auto h = proto::make_header(
				static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
				static_cast<std::uint16_t>(sizeof(res)));
			handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));

			spdlog::warn(
				"World enter denied. sid={} serial={} request_id={} pending_account_id={} pending_char_id={} resp_account_id={} resp_char_id={} result_kind={}",
				pending.sid,
				pending.serial,
				request_id,
				pending.account_id,
				pending.char_id,
				account_id,
				char_id,
				static_cast<int>(result_kind));
			return;
		}

		const auto bind_result = BindAuthenticatedWorldSessionForLogin(
			pending.account_id,
			pending.char_id,
			pending.sid,
			pending.serial,
			static_cast<std::uint16_t>(pt_w::WorldKickReason::duplicate_login));

		if (bind_result.kind == BindAuthedWorldSessionResultKind::InvalidInput) {
			res.ok = 0;
			res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::bind_invalid_input);
			const auto h = proto::make_header(
				static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
				static_cast<std::uint16_t>(sizeof(res)));
			handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));
			spdlog::warn(
				"OnWorldAuthTicketConsumeResponse bind failed. request_id={} sid={} serial={} account_id={} char_id={} bind_kind={} duplicate_cause={}",
				request_id,
				pending.sid,
				pending.serial,
				account_id,
				char_id,
				ToString(bind_result.kind),
				ToString(bind_result.duplicate_cause));
			return;
		}
		else {
			const std::uint32_t world_code = 0;
			svr::demo::DemoCharState st{};
			st.char_id = char_id;
			st.gold = 1000;
			st.version = 1;

			if (auto blob = TryLoadCharacterState(world_code, char_id))
			{
				svr::demo::DemoCharState loaded{};
				if (svr::demo::TryDeserializeDemo(*blob, loaded) && loaded.char_id == char_id)
				{
					st = loaded;
					spdlog::info("[Demo] loaded from redis char_id={} gold={} ver={}", st.char_id, st.gold, st.version);
				}
				else
				{
					spdlog::warn("[Demo] redis blob invalid, recreate char_id={}", char_id);
				}
			}
			else
			{
				spdlog::info("[Demo] no redis state, create new char_id={}", char_id);
			}

			st.gold += 10;
			st.version += 1;
			const std::string out_blob = svr::demo::SerializeDemo(st);

			constexpr std::uint32_t kDefaultMapTemplateId = 1001;
			constexpr std::uint32_t kDefaultInstanceId = 0;
			const auto map_assignment = AssignMapInstance(kDefaultMapTemplateId, kDefaultInstanceId, true, false);

			const std::uint32_t assigned_zone_id = map_assignment.zone_id != 0 ? map_assignment.zone_id : 1;

			PostActor(char_id, [this, char_id, sid = pending.sid, serial = pending.serial, assigned_zone_id]() {
				auto& a = GetOrCreatePlayerActor(char_id);
				a.bind_session(sid, serial);
				a.combat.hp = 100;
				a.combat.max_hp = 100;
				a.combat.atk = 20;
				a.combat.def = 3;
				a.combat.gold = 1000;
				a.zone_id = assigned_zone_id;
				a.map_template_id = kDefaultMapTemplateId;
				a.map_instance_id = kDefaultInstanceId;
				a.pos = { 0,0 };

				PostActor(svr::MakeZoneActorId(a.zone_id), [this, char_id, sid, serial, assigned_zone_id]() {
					auto& z = GetOrCreateZoneActor(assigned_zone_id);
					z.JoinOrUpdate(char_id, { 0,0 }, sid, serial);
				});
			});

			SendZonePlayerEnter_(static_cast<std::uint16_t>(assigned_zone_id), char_id, kDefaultMapTemplateId, kDefaultInstanceId);

			CacheCharacterState(world_code, char_id, out_blob);

			proto::S2C_actor_bound bound{};
			bound.actor_id = char_id;
			auto bh = proto::make_header(
				static_cast<std::uint16_t>(proto::S2CMsg::actor_bound),
				static_cast<std::uint16_t>(sizeof(bound)));
			handler->Send(0, pending.sid, pending.serial, bh, reinterpret_cast<const char*>(&bound));

			if (!NotifyAccountWorldEnterSuccess(account_id, char_id, pending.login_session, pending.world_token)) {
				const auto rollback = UnbindAuthenticatedWorldSessionBySid(pending.sid, pending.serial);
				ClosedAuthedSessionContext rollback_ctx{};
				rollback_ctx.unbind_kind = rollback.kind;
				rollback_ctx.sid = pending.sid;
				rollback_ctx.serial = pending.serial;
				CleanupClosedWorldSessionActors_(rollback_ctx);

				FailPendingEnterWorldConsumeRequest_(
					pending,
					pt_w::EnterWorldResultCode::internal_error,
					"OnWorldAuthTicketConsumeResponse rolled back because account world_enter_success_notify send failed.");
				return;
			}

			res.ok = 1;
			res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::success);
		}

		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));

		spdlog::info(
			"OnWorldAuthTicketConsumeResponse success. request_id={} sid={} serial={} account_id={} char_id={} bind_kind={} duplicate_cause={}",
			request_id,
			pending.sid,
			pending.serial,
			account_id,
			char_id,
			ToString(bind_result.kind),
			ToString(bind_result.duplicate_cause));
	}


	void WorldRuntime::LogSessionCloseEvent_(
		spdlog::level::level_enum level,
		std::string_view event_text,
		const SessionCloseLogContext& ctx) const
	{
		if (ctx.trace_id != 0) {
			spdlog::log(
				level,
				"[dup_login trace={}] {} char_id={} sid={} serial={}",
				ctx.trace_id,
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial);
		}
		else {
			spdlog::log(
				level,
				"[session_close] {} char_id={} sid={} serial={}",
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial);
		}
	}

	void WorldRuntime::LogSessionCloseProcessed_(
		spdlog::level::level_enum level,
		std::string_view event_text,
		const SessionCloseLogContext& ctx,
		bool removed) const
	{
		if (ctx.trace_id != 0) {
			spdlog::log(
				level,
				"[dup_login trace={}] {} char_id={} sid={} serial={} removed={}",
				ctx.trace_id,
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				static_cast<int>(removed));
		}
		else {
			spdlog::log(
				level,
				"[session_close] {} char_id={} sid={} serial={} removed={}",
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				static_cast<int>(removed));
		}
	}

	void WorldRuntime::LogDuplicateWorldSessionEvent_(
		spdlog::level::level_enum level,
		std::string_view event_text,
		const DuplicateLoginLogContext& ctx) const
	{
		spdlog::log(
			level,
			"[dup_login trace={}] {} account_id={} char_id={} sid={} serial={} new_sid={} new_serial={} packet_kick_reason={} log_cause={} stat_category={}",
			ctx.trace_id,
			event_text,
			ctx.account_id,
			ctx.char_id,
			ctx.sid,
			ctx.serial,
			ctx.new_sid,
			ctx.new_serial,
			ctx.packet_kick_reason,
			ToString(ctx.log_cause),
			ToString(ctx.stat_category));
	}

	void WorldRuntime::LogDuplicateWorldSessionCloseDecision_(
		spdlog::level::level_enum level,
		std::string_view decision_text,
		const DuplicateLoginLogContext& ctx) const
	{
		spdlog::log(
			level,
			"[dup_login trace={}] {} account_id={} char_id={} sid={} serial={} new_sid={} new_serial={} packet_kick_reason={} log_cause={} stat_category={}",
			ctx.trace_id,
			decision_text,
			ctx.account_id,
			ctx.char_id,
			ctx.sid,
			ctx.serial,
			ctx.new_sid,
			ctx.new_serial,
			ctx.packet_kick_reason,
			ToString(ctx.log_cause),
			ToString(ctx.stat_category));
	}

	std::optional<WorldAuthedSession> WorldRuntime::FindAuthenticatedWorldSessionBySid_(
		std::uint32_t sid) const
	{
		if (sid == 0) {
			return std::nullopt;
		}

		std::lock_guard lk(world_session_mtx_);

		auto it = authed_sessions_by_sid_.find(sid);
		if (it == authed_sessions_by_sid_.end()) {
			return std::nullopt;
		}

		return it->second;
	}

	std::optional<WorldAuthedSession> WorldRuntime::FindAuthenticatedWorldSessionByCharId_(
		std::uint64_t char_id) const
	{
		if (char_id == 0) {
			return std::nullopt;
		}

		std::lock_guard lk(world_session_mtx_);

		auto sid_it = authed_sid_by_char_id_.find(char_id);
		if (sid_it == authed_sid_by_char_id_.end()) {
			return std::nullopt;
		}

		auto session_it = authed_sessions_by_sid_.find(sid_it->second);
		if (session_it == authed_sessions_by_sid_.end()) {
			return std::nullopt;
		}

		return session_it->second;
	}

	WorldRuntime::SessionCloseLogContext
		WorldRuntime::MakeSessionCloseLogContext_(
			const ClosedAuthedSessionContext& closed_ctx,
			std::uint64_t trace_id,
			std::uint64_t fallback_char_id) noexcept
	{
		SessionCloseLogContext ctx{};
		ctx.trace_id = trace_id;
		ctx.char_id =
			(closed_ctx.char_id != 0)
			? closed_ctx.char_id
			: fallback_char_id;
		ctx.sid = closed_ctx.sid;
		ctx.serial = closed_ctx.serial;
		return ctx;
	}

	WorldRuntime::ClosedAuthedSessionContext
		WorldRuntime::MakeClosedAuthedSessionContext_(
			const UnbindAuthedWorldSessionResult& unbind_result,
			std::uint32_t sid,
			std::uint32_t serial) noexcept
	{
		ClosedAuthedSessionContext ctx{};
		ctx.unbind_kind = unbind_result.kind;
		ctx.account_id = unbind_result.session.account_id;
		ctx.char_id = unbind_result.session.char_id;
		ctx.sid = sid;
		ctx.serial = serial;
		return ctx;
	}

	WorldRuntime::SessionCloseLogContext
		WorldRuntime::ResolveWorldSessionCloseLogContext_(
			const ClosedAuthedSessionContext& closed_ctx,
			const DelayedCloseEntry* released_entry) noexcept
	{
		if (released_entry != nullptr) {
			return MakeSessionCloseLogContext_(
				closed_ctx,
				released_entry->log_ctx.trace_id,
				released_entry->log_ctx.char_id);
		}

		return MakeSessionCloseLogContext_(closed_ctx);
	}

	const char* WorldRuntime::ToString(BindAuthedWorldSessionResultKind kind) noexcept
	{
		switch (kind) {
		case BindAuthedWorldSessionResultKind::InvalidInput:
			return "InvalidInput";
		case BindAuthedWorldSessionResultKind::Inserted:
			return "Inserted";
		case BindAuthedWorldSessionResultKind::ReplacedCharSession:
			return "ReplacedCharSession";
		case BindAuthedWorldSessionResultKind::ReplacedAccountSession:
			return "ReplacedAccountSession";
		case BindAuthedWorldSessionResultKind::ReplacedBoth:
			return "ReplacedBoth";
		case BindAuthedWorldSessionResultKind::AlreadyBoundSameSession:
			return "AlreadyBoundSameSession";
		default:
			return "UnknownBindAuthedWorldSessionResult";
		}
	}

	const char* WorldRuntime::ToString(UnbindAuthedWorldSessionResultKind kind) noexcept
	{
		switch (kind) {
		case UnbindAuthedWorldSessionResultKind::InvalidInput:
			return "InvalidInput";
		case UnbindAuthedWorldSessionResultKind::NotFoundBySid:
			return "NotFoundBySid";
		case UnbindAuthedWorldSessionResultKind::SerialMismatch:
			return "SerialMismatch";
		case UnbindAuthedWorldSessionResultKind::Removed:
			return "Removed";
		default:
			return "UnknownUnbindAuthedWorldSessionResult";
		}
	}

	const char* WorldRuntime::ToString(DuplicateSessionCause cause) noexcept
	{
		switch (cause) {
		case DuplicateSessionCause::None:
			return "None";
		case DuplicateSessionCause::DuplicateCharSession:
			return "DuplicateCharSession";
		case DuplicateSessionCause::DuplicateAccountSession:
			return "DuplicateAccountSession";
		case DuplicateSessionCause::DuplicateCharAndAccountSession:
			return "DuplicateCharAndAccountSession";
		default:
			return "UnknownDuplicateSessionCause";
		}
	}

	const char* WorldRuntime::ToString(SessionKickStatCategory category) noexcept
	{
		switch (category) {
		case SessionKickStatCategory::None:
			return "None";
		case SessionKickStatCategory::DuplicateChar:
			return "DuplicateChar";
		case SessionKickStatCategory::DuplicateAccount:
			return "DuplicateAccount";
		case SessionKickStatCategory::DuplicateBoth:
			return "DuplicateBoth";
		case SessionKickStatCategory::DuplicateDeduplicatedSameSession:
			return "DuplicateDeduplicatedSameSession";
		case SessionKickStatCategory::Other:
			return "Other";
		default:
			return "UnknownSessionKickStatCategory";
		}
	}

	void WorldRuntime::EnqueueDuplicateWorldSessionKickClose_(
		DuplicateLoginLogContext ctx)
	{
		if (ctx.char_id == 0 || ctx.sid == 0 || ctx.serial == 0) {
			return;
		}

		LogDuplicateWorldSessionEvent_(
			spdlog::level::info,
			"enqueue serialized old-session kick/close",
			ctx);

		boost::asio::dispatch(
			duplicate_session_strand_,
			[this, ctx]() {
			ProcessDuplicateWorldSessionKickOnIo_(ctx);
		});
	}

	void WorldRuntime::EnqueueDuplicateAuthedSessionCloseIfNeeded_(
		const WorldAuthedSession& victim,
		std::uint64_t fallback_account_id,
		std::uint64_t fallback_char_id,
		std::uint32_t new_sid,
		std::uint32_t new_serial,
		std::uint16_t packet_kick_reason,
		DuplicateSessionCause log_cause,
		SessionKickStatCategory stat_category)
	{
		if (victim.sid == 0 || victim.serial == 0) {
			return;
		}

		if (victim.sid == new_sid && victim.serial == new_serial) {
			spdlog::info(
				"Skip duplicate authenticated session close enqueue. log_cause={} stat_category={} victim is same as new session. account_id={} char_id={} sid={} serial={}",
				ToString(log_cause),
				ToString(stat_category),
				victim.account_id,
				victim.char_id,
				victim.sid,
				victim.serial);
			return;
		}

		// TODO:
		// stat_category 기준 운영 카운터/메트릭 집계를 여기에 연결한다.
		// 예: duplicate_char / duplicate_account / duplicate_both / deduplicated_same_session
		DuplicateLoginLogContext ctx{};
		ctx.trace_id = duplicate_login_trace_seq_.fetch_add(1, std::memory_order_relaxed);
		ctx.account_id = victim.account_id != 0 ? victim.account_id : fallback_account_id;
		ctx.char_id = victim.char_id != 0 ? victim.char_id : fallback_char_id;
		ctx.sid = victim.sid;
		ctx.serial = victim.serial;
		ctx.new_sid = new_sid;
		ctx.new_serial = new_serial;
		ctx.packet_kick_reason = packet_kick_reason;
		ctx.log_cause = log_cause;
		ctx.stat_category = stat_category;

		spdlog::info(
			"Enqueue duplicate authenticated session close. log_cause={} stat_category={} packet_kick_reason={} trace_id={} victim_account_id={} victim_char_id={} victim_sid={} victim_serial={} new_sid={} new_serial={}",
			ToString(ctx.log_cause),
			ToString(ctx.stat_category),
			ctx.packet_kick_reason,
			ctx.trace_id,
			ctx.account_id,
			ctx.char_id,
			ctx.sid,
			ctx.serial,
			ctx.new_sid,
			ctx.new_serial);

		EnqueueDuplicateWorldSessionKickClose_(ctx);
	}

	bool WorldRuntime::TryBeginDuplicateWorldSessionKickClose_(
		const DuplicateLoginLogContext& ctx)
	{
		if (!TryReserveDelayedWorldClose_(ctx.sid, ctx.serial)) {
			LogDuplicateWorldSessionEvent_(
				spdlog::level::info,
				"close reservation already exists -> skip duplicate kick",
				ctx);
			return false;
		}

		LogDuplicateWorldSessionEvent_(
			spdlog::level::info,
			"close reservation acquired",
			ctx);

		return true;
	}

	template<class HandlerT>
	bool WorldRuntime::SendDuplicateWorldSessionKick_(
		const DuplicateLoginLogContext& ctx,
		HandlerT& world_handler)
	{
		pt_w::S2C_kick_notify kick{};
		kick.reason = ctx.packet_kick_reason;
		kick.char_id = ctx.char_id;

		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::kick_notify),
			static_cast<std::uint16_t>(sizeof(kick)));

		return world_handler.Send(
			static_cast<std::uint32_t>(svr::WorldLineId::World),
			ctx.sid,
			ctx.serial,
			h,
			reinterpret_cast<const char*>(&kick));
	}

	template<class HandlerT>
	void WorldRuntime::CloseDuplicateWorldSessionImmediately_(
		const DuplicateLoginLogContext& ctx,
		std::string_view reason,
		HandlerT& world_handler)
	{

		if (!UpdateReservedDelayedWorldCloseContext_(
			ctx.sid,
			ctx.serial,
			ctx.trace_id,
			ctx.char_id)) {
			spdlog::warn(
				"[dup_login trace={}] immediate close lost delayed-close reservation context. char_id={} sid={} serial={}",
				ctx.trace_id,
				ctx.char_id,
				ctx.sid,
				ctx.serial);
		}

		LogDuplicateWorldSessionCloseDecision_(
			spdlog::level::warn,
			reason,
			ctx);

		world_handler.Close(
			static_cast<std::uint32_t>(svr::WorldLineId::World),
			ctx.sid,
			ctx.serial);
	}

	bool WorldRuntime::CancelDelayedWorldCloseTimer_(
		std::uint32_t sid,
		std::uint32_t serial) noexcept
	{
		if (sid == 0 || serial == 0) {
			return false;
		}

		std::shared_ptr<boost::asio::steady_timer> timer;

		{
			std::lock_guard lk(delayed_world_close_mtx_);

			const DelayedCloseKey key{ sid, serial };
			auto it = delayed_world_close_entries_.find(key);
			if (it == delayed_world_close_entries_.end()) {
				return false;
			}

			timer = std::move(it->second.timer);
			delayed_world_close_entries_.erase(it);
		}

		if (timer) {
			timer->cancel();
		}

		return true;
	}

	std::uint32_t WorldRuntime::GetActiveWorldSessionCount() const
	{
		std::lock_guard lk(world_session_mtx_);
		return static_cast<std::uint32_t>(authed_sessions_by_sid_.size());
	}

	std::uint16_t WorldRuntime::GetActiveZoneCount() const
	{
		std::lock_guard lk(service_line_mtx_);
		const auto count = static_cast<std::uint16_t>(zone_routes_by_sid_.size());
		return count == 0 ? 1 : count;
	}

	void WorldRuntime::SendAccountRouteHeartbeat_()
	{
		if (!account_ready_.load(std::memory_order_acquire)) {
			return;
		}

		if (!world_account_handler_) {
			spdlog::debug("WorldRuntime skipped account route heartbeat: world_account_handler_ is null");
			return;
		}

		const auto sid = account_sid_.load(std::memory_order_relaxed);
		const auto serial = account_serial_.load(std::memory_order_relaxed);
		if (sid == 0 || serial == 0) {
			return;
		}

		if (!world_account_handler_->SendRouteHeartbeat(0, sid, serial)) {
			spdlog::debug("WorldRuntime failed to send account route heartbeat. sid={} serial={}", sid, serial);
		}
	}

	void WorldRuntime::RegisterZoneRoute(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t zone_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t map_instance_capacity,
		std::uint16_t active_map_instance_count,
		std::uint16_t active_player_count,
		std::uint16_t load_score,
		std::uint32_t flags,
		std::string_view server_name)
	{
		std::lock_guard lk(service_line_mtx_);
		auto& route = zone_routes_by_sid_[sid];
		route.registered = true;
		route.sid = sid;
		route.serial = serial;
		route.server_id = server_id;
		route.zone_id = zone_id;
		route.world_id = world_id;
		route.channel_id = channel_id;
		route.map_instance_capacity = map_instance_capacity;
		route.active_map_instance_count = active_map_instance_count;
		route.active_player_count = active_player_count;
		route.load_score = load_score;
		route.flags = flags;
		route.server_name.assign(server_name.begin(), server_name.end());
		route.last_heartbeat_at = std::chrono::steady_clock::now();

		spdlog::info(
			"WorldRuntime zone route registered. sid={} serial={} server_id={} zone_id={} world_id={} channel_id={} active_maps={} active_players={} capacity={} load={} flags={} name={}",
			sid, serial, server_id, zone_id, world_id, channel_id, active_map_instance_count, active_player_count, map_instance_capacity, load_score, flags, server_name);
	}

	void WorldRuntime::OnZoneRouteHeartbeat(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t zone_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t map_instance_capacity,
		std::uint16_t active_map_instance_count,
		std::uint16_t active_player_count,
		std::uint16_t load_score,
		std::uint32_t flags)
	{
		std::lock_guard lk(service_line_mtx_);
		auto it = zone_routes_by_sid_.find(sid);
		if (it == zone_routes_by_sid_.end()) {
			return;
		}
		if (it->second.serial != serial) {
			return;
		}

		auto& route = it->second;
		route.server_id = server_id;
		route.zone_id = zone_id;
		route.world_id = world_id;
		route.channel_id = channel_id;
		route.map_instance_capacity = map_instance_capacity;
		route.active_map_instance_count = active_map_instance_count;
		route.active_player_count = active_player_count;
		route.load_score = load_score;
		route.flags = flags;
		route.last_heartbeat_at = std::chrono::steady_clock::now();
	}

	void WorldRuntime::UnregisterZoneRoute(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		std::lock_guard lk(service_line_mtx_);
		auto it = zone_routes_by_sid_.find(sid);
		if (it == zone_routes_by_sid_.end()) {
			return;
		}
		if (it->second.serial != serial) {
			return;
		}

		spdlog::info(
			"WorldRuntime zone route removed. sid={} serial={} server_id={} zone_id={} world_id={} channel_id={}",
			it->second.sid, it->second.serial, it->second.server_id, it->second.zone_id, it->second.world_id, it->second.channel_id);
		zone_routes_by_sid_.erase(it);
	}

	void WorldRuntime::ExpireStaleZoneRoutes_(std::chrono::steady_clock::time_point now)
	{
		std::lock_guard lk(service_line_mtx_);
		for (auto it = zone_routes_by_sid_.begin(); it != zone_routes_by_sid_.end();) {
			if (it->second.last_heartbeat_at != std::chrono::steady_clock::time_point{} &&
				now - it->second.last_heartbeat_at > std::chrono::seconds(8)) {
				spdlog::warn(
					"WorldRuntime zone route heartbeat expired. sid={} serial={} server_id={} zone_id={}",
					it->second.sid, it->second.serial, it->second.server_id, it->second.zone_id);
				it = zone_routes_by_sid_.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = pending_zone_assign_requests_.begin(); it != pending_zone_assign_requests_.end();) {
			if (now - it->second.issued_at > std::chrono::seconds(3)) {
				it = pending_zone_assign_requests_.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = pending_zone_assign_requests_.begin(); it != pending_zone_assign_requests_.end();) {
			if (now - it->second.issued_at > std::chrono::seconds(3)) {
				it = pending_zone_assign_requests_.erase(it);
			}
			else {
				++it;
			}
		}
	}

	void WorldRuntime::MarkAccountRegistered_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint32_t server_id,
		std::uint16_t world_id,
		std::uint16_t channel_id,
		std::uint16_t active_zone_count,
		std::uint16_t load_score,
		std::uint32_t flags,
		std::string_view server_name,
		std::string_view public_host,
		std::uint16_t public_port)
	{
		account_sid_.store(sid, std::memory_order_relaxed);
		account_serial_.store(serial, std::memory_order_relaxed);
		account_ready_.store(true, std::memory_order_release);
		next_account_route_heartbeat_tp_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);

		spdlog::info(

			"WorldRuntime account line ready. sid={} serial={} server_id={} world_id={} channel_id={} zones={} load_score={} flags={} server_name={} public_host={} public_port={}",
			sid, serial, server_id, world_id, channel_id, active_zone_count, load_score, flags, server_name, public_host, public_port);
	}

	void WorldRuntime::MarkAccountDisconnected_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		const auto cur_sid = account_sid_.load(std::memory_order_relaxed);
		const auto cur_serial = account_serial_.load(std::memory_order_relaxed);

		if (cur_sid != sid || cur_serial != serial) {
			return;
		}

		account_ready_.store(false, std::memory_order_release);
		account_sid_.store(0, std::memory_order_relaxed);
		account_serial_.store(0, std::memory_order_relaxed);
	}

	bool WorldRuntime::NotifyAccountWorldEnterSuccess(
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		if (!account_ready_.load(std::memory_order_acquire)) {
			spdlog::warn(
				"NotifyAccountWorldEnterSuccess skipped: account line not ready. account_id={} char_id={}",
				account_id,
				char_id);
			return false;
		}

		if (!world_account_handler_) {
			spdlog::warn(
				"NotifyAccountWorldEnterSuccess skipped: world_account_handler_ is null. account_id={} char_id={}",
				account_id,
				char_id);
			return false;
		}

		return world_account_handler_->SendWorldEnterSuccessNotify(
			100,
			account_sid_.load(std::memory_order_relaxed),
			account_serial_.load(std::memory_order_relaxed),
			account_id,
			char_id,
			login_session,
			world_token);
	}

}
