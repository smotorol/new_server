#include "services/world/runtime/world_runtime_private.h"
#include <cstdlib>
#include "server_common/data/zone_runtime_data.h"
#include "server_common/config/aoi_config.h"
#include "services/world/zone_loader/zone_content_catalog.h"
#include <thread>

namespace svr {

	WorldRuntime g_Main;

	using namespace svr::detail;

	static std::unique_ptr<net::IActor> MakeServerActor_(std::uint64_t id)
	{
		auto aoi_ini_cfg = dc::cfg::GetAoiConfig();
		if (id == 0) return std::make_unique<svr::WorldActor>();
		if (svr::IsZoneActorId(id)) {
			auto z = std::make_unique<svr::ZoneActor>();
			// ✅ eos_royal 스타일: 섹터 컨테이너 초기화(ini 기반)
			z->InitSectorSystem(aoi_ini_cfg.map_size,
				(std::int32_t)aoi_ini_cfg.world_sight_unit,
				(std::int32_t)aoi_ini_cfg.aoi_radius_cells);
			return z;
		}
		return std::make_unique<svr::PlayerActor>(id);
	}

	WorldRuntime::WorldRuntime() = default;

	WorldRuntime::~WorldRuntime() {
		ReleaseMainThread();
	}

	bool WorldRuntime::OnRuntimeInit() {
		if (!LoadIniFile()) {
			spdlog::warn("LoadIniFile() failed (stub). Continue with defaults.");
		}

		const char* legacyFallbackEnv = std::getenv("DC_ALLOW_LEGACY_ITEM_TEMPLATE_FALLBACK");
		if (legacyFallbackEnv != nullptr) {
			allow_legacy_item_template_fallback_ = !(legacyFallbackEnv[0] == '0' || legacyFallbackEnv[0] == 'f' || legacyFallbackEnv[0] == 'F');
		}

		spdlog::info("WorldRuntime waiting for account register ack before DB initialization.");

        if (!dc::zone::ZoneRuntimeDataStore::LoadFromBinary(dc::zone::DefaultZoneRuntimeBinaryPath())) {
            auto zone_status = dc::zone::ZoneRuntimeDataStore::SnapshotStatus();
            spdlog::warn("Zone runtime binary load failed. source={} reason={}", zone_status.source, zone_status.last_error_reason);
        }
        else {
            auto zone_status = dc::zone::ZoneRuntimeDataStore::SnapshotStatus();
            spdlog::info("Zone runtime binary ready. source={} version={} maps={} portals={} npcs={} monsters={} ready={} empty={}", zone_status.source, zone_status.version, zone_status.preload_count, zone_status.portal_count, zone_status.npc_count, zone_status.monster_count, zone_status.ready ? 1 : 0, zone_status.empty ? 1 : 0);
        }

        if (!svr::zonecontent::LoadCatalog()) {
            spdlog::warn("Zone content catalog load failed or empty. root={}", svr::zonecontent::DefaultResourcesRoot().string());
        }
        else {
            const auto entries = svr::zonecontent::Snapshot();
            for (const auto& entry : entries) {
                spdlog::info(
                    "Zone content catalog entry ready. zone_id={} map_id={} wm_present={} portals={} npcs={} monsters={} safe={} special={} dir={}",
                    entry.zone_id,
                    entry.map_id,
                    std::filesystem::exists(entry.map_wm_path) ? 1 : 0,
                    entry.portal_count,
                    entry.npc_count,
                    entry.monster_count,
                    entry.safe_count,
                    entry.special_count,
                    entry.directory.string());
            }
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

		next_stat_tp_ = std::chrono::steady_clock::now() + dc::k_next_stat_tp_;
		last_move_pkts_ = 0;
		last_move_items_ = 0;

		spdlog::info("WorldServer initialized. ports: world={}, zone={}, control={}",
			port_world_, port_zone_, port_control_);
		return true;
	}



	void WorldRuntime::OnBeforeIoStop() {
		spdlog::info("[shutdown] step=1 stop_accept_and_block_new_sessions");
		lines_.stop_all_reverse();

		spdlog::info("[shutdown] step=2 stop_periodic_flush_scheduler");
		flush_timer_.cancel();

		spdlog::info("[shutdown] step=3 enqueue_final_dirty_flush");
		EnqueueFlushDirty_(/*immediate=*/true);

		spdlog::info("[shutdown] step=3.1 wait_dqs_drain_begin");
		const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		std::size_t last_in_flight = CountInFlightDqs_();
		while (last_in_flight != 0 && std::chrono::steady_clock::now() < drain_deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			last_in_flight = CountInFlightDqs_();
		}
		spdlog::info(
			"[shutdown] step=3.2 wait_dqs_drain_end in_flight={} timed_out={}",
			last_in_flight,
			(last_in_flight != 0 ? 1 : 0));
		if (last_in_flight != 0) {
			spdlog::warn(
				"[shutdown] dqs drain timed out. in_flight={} (continuing shutdown with forced worker stop)",
				last_in_flight);
		}

		spdlog::info("[shutdown] step=4 cancel_delayed_close_timers");
		CancelDelayedWorldCloseTimers_();

		spdlog::info("[shutdown] step=5 stop_db_workers");
		if (db_shards_) {
			db_shards_->stop();
			db_shards_.reset();
		}

		spdlog::info("[shutdown] step=6 stop_actor_workers");
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

		pending_character_enter_snapshot_requests_.clear();
		pending_enter_world_finalize_by_assign_request_.clear();
		pending_zone_player_enter_requests_.clear();

		{
			std::lock_guard lk(delayed_world_close_mtx_);
			delayed_world_close_entries_.clear();
		}

		{
			std::lock_guard lk(world_session_mtx_);
			authed_sessions_by_sid_.clear();
			authed_session_key_by_char_id_.clear();
			authed_session_key_by_account_id_.clear();
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

		spdlog::info("[shutdown] step=7 io_stopped_cleanup_complete");
		spdlog::info("WorldServer released.");
	}



	void WorldRuntime::PostActor(std::uint64_t actor_id, std::function<void()> fn)
	{
		if (!fn) return;
		actors_.post(actor_id, std::move(fn));
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
			next_account_route_heartbeat_tp_ = now + dc::k_next_account_route_heartbeat_tp;
			SendAccountRouteHeartbeat_();
		}

		ExpireStaleZoneRoutes_(now);

		if (now >= next_stat_tp_) {
			next_stat_tp_ = now + dc::k_next_stat_tp_;

			const auto cur_pkts = svr::metrics::g_s2c_move_pkts_sent.load(std::memory_order_relaxed);
			const auto cur_items = svr::metrics::g_s2c_move_items_sent.load(std::memory_order_relaxed);
			const auto d_pkts = cur_pkts - last_move_pkts_;
			const auto d_items = cur_items - last_move_items_;
			last_move_pkts_ = cur_pkts;
			last_move_items_ = cur_items;

			if (d_pkts > 0 || d_items > 0) {
				spdlog::info("[netstats] s2c_move pkts/s={} items/s={}", d_pkts, d_items);
			}

			const auto cur_entered = svr::metrics::g_aoi_entered_entities.load(std::memory_order_relaxed);
			const auto cur_exited = svr::metrics::g_aoi_exited_entities.load(std::memory_order_relaxed);
			const auto cur_fanout = svr::metrics::g_aoi_move_fanout.load(std::memory_order_relaxed);
			const auto cur_events = svr::metrics::g_aoi_move_events.load(std::memory_order_relaxed);
			const auto cur_sanitize_removed_entered = svr::metrics::g_aoi_sanitize_removed_entered.load(std::memory_order_relaxed);
			const auto cur_sanitize_removed_exited = svr::metrics::g_aoi_sanitize_removed_exited.load(std::memory_order_relaxed);
			const auto cur_sanitize_removed_new_vis = svr::metrics::g_aoi_sanitize_removed_new_vis.load(std::memory_order_relaxed);
			const auto cur_unauth_rejects = svr::metrics::g_world_unauth_packet_rejects.load(std::memory_order_relaxed);
				const auto cur_dup_char = svr::metrics::g_dup_login_char.load(std::memory_order_relaxed);
				const auto cur_dup_account = svr::metrics::g_dup_login_account.load(std::memory_order_relaxed);
				const auto cur_dup_both = svr::metrics::g_dup_login_both.load(std::memory_order_relaxed);
				const auto cur_dup_dedup = svr::metrics::g_dup_login_dedup_same_session.load(std::memory_order_relaxed);
				const auto cur_flush_dirty_conflicts = svr::metrics::g_flush_dirty_conflicts_total.load(std::memory_order_relaxed);
				const auto cur_flush_dirty_conflicted_batches = svr::metrics::g_flush_dirty_conflicted_batches.load(std::memory_order_relaxed);

			const auto d_entered = cur_entered - last_aoi_entered_entities_;
			const auto d_exited = cur_exited - last_aoi_exited_entities_;
			const auto d_fanout = cur_fanout - last_aoi_move_fanout_;
			const auto d_events = cur_events - last_aoi_move_events_;
			const auto d_sanitize_removed_entered = cur_sanitize_removed_entered - last_aoi_sanitize_removed_entered_;
			const auto d_sanitize_removed_exited = cur_sanitize_removed_exited - last_aoi_sanitize_removed_exited_;
			const auto d_sanitize_removed_new_vis = cur_sanitize_removed_new_vis - last_aoi_sanitize_removed_new_vis_;
			const auto d_unauth_rejects = cur_unauth_rejects - last_unauth_packet_rejects_;
				const auto d_dup_char = cur_dup_char - last_dup_login_char_;
				const auto d_dup_account = cur_dup_account - last_dup_login_account_;
				const auto d_dup_both = cur_dup_both - last_dup_login_both_;
				const auto d_dup_dedup = cur_dup_dedup - last_dup_login_dedup_same_session_;
				const auto d_flush_dirty_conflicts = cur_flush_dirty_conflicts - last_flush_dirty_conflicts_total_;
				const auto d_flush_dirty_conflicted_batches = cur_flush_dirty_conflicted_batches - last_flush_dirty_conflicted_batches_;

			last_aoi_entered_entities_ = cur_entered;
			last_aoi_exited_entities_ = cur_exited;
			last_aoi_move_fanout_ = cur_fanout;
			last_aoi_move_events_ = cur_events;
			last_aoi_sanitize_removed_entered_ = cur_sanitize_removed_entered;
			last_aoi_sanitize_removed_exited_ = cur_sanitize_removed_exited;
			last_aoi_sanitize_removed_new_vis_ = cur_sanitize_removed_new_vis;
			last_unauth_packet_rejects_ = cur_unauth_rejects;
				last_dup_login_char_ = cur_dup_char;
				last_dup_login_account_ = cur_dup_account;
				last_dup_login_both_ = cur_dup_both;
				last_dup_login_dedup_same_session_ = cur_dup_dedup;
				last_flush_dirty_conflicts_total_ = cur_flush_dirty_conflicts;
				last_flush_dirty_conflicted_batches_ = cur_flush_dirty_conflicted_batches;

			if (d_events > 0 || d_entered > 0 || d_exited > 0 || d_sanitize_removed_entered > 0 || d_sanitize_removed_exited > 0 || d_sanitize_removed_new_vis > 0) {
				const double avg_fanout = (d_events == 0)
					? 0.0
					: static_cast<double>(d_fanout) / static_cast<double>(d_events);
				spdlog::info(
					"[aoistats] moves/s={} fanout/s={} avg_fanout={:.2f} entered/s={} exited/s={} sanitize_removed/s(entered={},exited={},new_vis={})",
					d_events,
					d_fanout,
					avg_fanout,
					d_entered,
					d_exited,
					d_sanitize_removed_entered,
					d_sanitize_removed_exited,
					d_sanitize_removed_new_vis);
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

			constexpr std::uint64_t kUnauthWarnThresholdPerSec = 10;
			if (d_unauth_rejects >= kUnauthWarnThresholdPerSec) {
				const auto sampled_sid = svr::metrics::g_world_unauth_last_sid.load(std::memory_order_relaxed);
				spdlog::warn(
					"[authstats] unauth_packet_rejects/s={} threshold={} sampled_sid={}",
					d_unauth_rejects,
					kUnauthWarnThresholdPerSec,
					sampled_sid);
			}
			else if (d_unauth_rejects > 0) {
				spdlog::info("[authstats] unauth_packet_rejects/s={}", d_unauth_rejects);
			}

				if (d_dup_char > 0 || d_dup_account > 0 || d_dup_both > 0 || d_dup_dedup > 0) {
					spdlog::info(
						"[dupstats] char/s={} account/s={} both/s={} dedup_same/s={}",
						d_dup_char,
						d_dup_account,
						d_dup_both,
						d_dup_dedup);
				}

				if (d_flush_dirty_conflicts > 0 || d_flush_dirty_conflicted_batches > 0) {
					spdlog::info(
						"[flushstats] dirty_conflicts/s={} conflicted_batches/s={} est_conflicts/min={}",
						d_flush_dirty_conflicts,
						d_flush_dirty_conflicted_batches,
						d_flush_dirty_conflicts * 60ULL);
				}

				constexpr std::uint64_t kFlushDirtyConflictWarnThresholdPerMin = 60;
				const auto est_conflicts_per_min = d_flush_dirty_conflicts * 60ULL;
				if (est_conflicts_per_min >= kFlushDirtyConflictWarnThresholdPerMin) {
					const auto sample_world = svr::metrics::g_flush_dirty_conflict_world_sample.load(std::memory_order_relaxed);
					const auto sample_shard = svr::metrics::g_flush_dirty_conflict_shard_sample.load(std::memory_order_relaxed);
					const auto sample_char = svr::metrics::g_flush_dirty_conflict_char_sample.load(std::memory_order_relaxed);
					spdlog::warn(
						"[flushstats] high_conflict_rate est_conflicts/min={} threshold={} sample_world={} sample_shard={} sample_char={}",
						est_conflicts_per_min,
						kFlushDirtyConflictWarnThresholdPerMin,
						sample_world,
						sample_shard,
						sample_char);
				}
			}
		}

} // namespace svr



