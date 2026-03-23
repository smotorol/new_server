#include "services/world/runtime/world_runtime_private.h"
#include "server_common/session/session_key.h"
#include "server_common/config/aoi_config.h"
#include "server_common/config/runtime_ini_schema.h"
#include "server_common/config/runtime_ini_sanity.h"
#include "server_common/config/runtime_ini_version.h"
#include "server_common/log/flow_event_codes.h"

namespace svr {

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
			constexpr int kDefaultReconnectGraceCloseDelayMs = 5000;
			bool config_fail_fast = false;
			int config_schema_version = dc::cfg::kRuntimeConfigSchemaVersion;
			std::string parse_error;
			std::string parse_warn;

			auto parse_int_field = [&](const std::string& key, const std::string& raw, int& target) -> bool {
				parse_error.clear();
				parse_warn.clear();
				if (!dc::cfg::ParseIntOrKeep(key.c_str(), raw, target, config_fail_fast, &parse_error, &parse_warn)) {
					spdlog::error("[config] {}", parse_error);
					return false;
				}
				if (!parse_warn.empty()) {
					spdlog::warn("[config] {}", parse_warn);
				}
				return true;
			};
			auto parse_u32_field = [&](const std::string& key, const std::string& raw, std::uint32_t& target) -> bool {
				parse_error.clear();
				parse_warn.clear();
				if (!dc::cfg::ParseU32OrKeep(key.c_str(), raw, target, config_fail_fast, &parse_error, &parse_warn)) {
					spdlog::error("[config] {}", parse_error);
					return false;
				}
				if (!parse_warn.empty()) {
					spdlog::warn("[config] {}", parse_warn);
				}
				return true;
			};

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
			if (!parse_int_field("REDIS.Port", v, redis_port_)) return false;
		}
		{
			auto v = ini.sections["REDIS"]["DB"];
			if (!parse_int_field("REDIS.DB", v, redis_db_)) return false;
		}
		redis_password_ = ini.sections["REDIS"]["Password"];
		{
			auto v = ini.sections["REDIS"]["Prefix"];
			if (!v.empty()) redis_prefix_ = v;
		}

		// ✅ Redis shard + WAIT 옵션
		{
			auto v = ini.sections["REDIS"]["SHARD_COUNT"];
			if (!parse_u32_field("REDIS.SHARD_COUNT", v, redis_shard_count_)) return false;
		}
		{
			auto v = ini.sections["REDIS"]["WAIT_REPLICAS"];
			if (!parse_int_field("REDIS.WAIT_REPLICAS", v, redis_wait_replicas_)) return false;
		}
		{
			auto v = ini.sections["REDIS"]["WAIT_TIMEOUT_MS"];
			if (!parse_int_field("REDIS.WAIT_TIMEOUT_MS", v, redis_wait_timeout_ms_)) return false;
		}

		// ✅ write-behind 튜닝
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_INTERVAL_SEC"];
			if (!parse_int_field("WRITE_BEHIND.FLUSH_INTERVAL_SEC", v, flush_interval_sec_)) return false;
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_BATCH_IMMEDIATE"];
			if (!parse_u32_field("WRITE_BEHIND.FLUSH_BATCH_IMMEDIATE", v, flush_batch_immediate_)) return false;
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["FLUSH_BATCH_NORMAL"];
			if (!parse_u32_field("WRITE_BEHIND.FLUSH_BATCH_NORMAL", v, flush_batch_normal_)) return false;
		}
		{
			auto v = ini.sections["WRITE_BEHIND"]["CHAR_TTL_SEC"];
			if (!parse_int_field("WRITE_BEHIND.CHAR_TTL_SEC", v, char_ttl_sec_)) return false;
		}
		{
			auto v = ini.sections["SESSION"]["RECONNECT_GRACE_CLOSE_DELAY_MS"];
			if (!parse_int_field("SESSION.RECONNECT_GRACE_CLOSE_DELAY_MS", v, reconnect_grace_close_delay_ms_)) return false;
		}

		// ✅ DB/DQS 샤딩 관련
		{
			auto v = ini.sections["DB_WORK"]["DB_POOL_SIZE_PER_WORLD"];
			if (!parse_int_field("DB_WORK.DB_POOL_SIZE_PER_WORLD", v, db_pool_size_per_world_)) return false;
		}
		{
			auto v = ini.sections["DB_WORK"]["DB_SHARD_COUNT"];
			if (!parse_u32_field("DB_WORK.DB_SHARD_COUNT", v, db_shard_count_)) return false;
		}

		// [World]
		worldset_num_ = 0;
		{
			auto v = ini.sections["World"]["WorldSet_Num"];
			if (!parse_int_field("World.WorldSet_Num", v, worldset_num_)) return false;
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
					int parsed_port = 0;
					if (!parse_int_field(std::string("World.") + key("Port"), port, parsed_port)) return false;
					w.port = parsed_port;
				}

				{
					auto idx = ini.sections["World"][key("WorldIdx")];
					int parsed_idx = 0;
					if (!parse_int_field(std::string("World.") + key("WorldIdx"), idx, parsed_idx)) return false;
					w.world_idx = parsed_idx;
				}

			worlds_.push_back(std::move(w));
		}

		// [NET_WORK]
		world_to_log_recv_buffer_size_ = 10'000'000;
		{
			auto v = ini.sections["NET_WORK"]["WORLD_TO_LOG_RECV_BUFFER_SIZE"];
			if (!parse_u32_field("NET_WORK.WORLD_TO_LOG_RECV_BUFFER_SIZE", v, world_to_log_recv_buffer_size_)) return false;
		}
		// ✅ io_context run() 스레드 개수(기본 1)
		{
			auto v = ini.sections["NET_WORK"]["IO_THREAD_COUNT"];
			if (!parse_int_field("NET_WORK.IO_THREAD_COUNT", v, io_thread_count_)) return false;
		}

		// ✅ 로직(Actor) 스레드 개수(기본 1)
		{
			auto v = ini.sections["NET_WORK"]["LOGIC_THREAD_COUNT"];
			if (!parse_int_field("NET_WORK.LOGIC_THREAD_COUNT", v, logic_thread_count_)) return false;
		}
		{
			auto v = ini.sections["SYSTEM"]["CONFIG_FAIL_FAST"];
			if (!v.empty()) {
				int parsed = 0;
					if (dc::cfg::TryParseInt(v, parsed)) {
						config_fail_fast = (parsed != 0);
					}
					else {
						spdlog::warn("[config] invalid SYSTEM.CONFIG_FAIL_FAST='{}' -> default(false)", v);
					}
			}
		}
		{
			auto v = ini.sections["SYSTEM"]["CONFIG_SCHEMA_VERSION"];
			if (!v.empty()) {
				int parsed = config_schema_version;
				if (!parse_int_field("SYSTEM.CONFIG_SCHEMA_VERSION", v, parsed)) return false;
				config_schema_version = parsed;
			}
		}

		// [AOI] (선택)
		// - MAP_W/MAP_H : 임시 맵 크기(월드/존마다 다르면 확장 가능)
		// - WORLD_SIGHT_UNIT : 셀 단위(레거시 WORLD_SIGHT_UNIT)
		// - AOI_RADIUS_CELLS : 주변 셀 반경(1이면 3x3)
		auto g_aoi_ini_cfg = dc::cfg::GetAoiConfig();

		{
			auto v = ini.sections["AOI"]["MAP_W"];
			if (!parse_int_field("AOI.MAP_W", v, g_aoi_ini_cfg.map_size.x)) return false;
		}
		{
			auto v = ini.sections["AOI"]["MAP_H"];
			if (!parse_int_field("AOI.MAP_H", v, g_aoi_ini_cfg.map_size.y)) return false;
		}
		{
			auto v = ini.sections["AOI"]["WORLD_SIGHT_UNIT"];
			if (!parse_int_field("AOI.WORLD_SIGHT_UNIT", v, g_aoi_ini_cfg.world_sight_unit)) return false;
		}
		{
			auto v = ini.sections["AOI"]["AOI_RADIUS_CELLS"];
			if (!parse_int_field("AOI.AOI_RADIUS_CELLS", v, g_aoi_ini_cfg.aoi_radius_cells)) return false;
		}

		// ============================================================
		// ✅ normalize / default rules (최종 확정값 계산)
		// ============================================================

		// 1~3) shard/wait sanity
		dc::cfg::NormalizeShardAndRedisWait(
			db_shard_count_,
			redis_shard_count_,
			redis_wait_replicas_,
			redis_wait_timeout_ms_);

		// 4) flush interval/batch/ttl sanity
		std::string policy_error;
		dc::cfg::WorldRuntimePolicyTargets policy_targets{};
		policy_targets.flush_interval_sec = &flush_interval_sec_;
		policy_targets.char_ttl_sec = &char_ttl_sec_;
		policy_targets.db_pool_size_per_world = &db_pool_size_per_world_;
		policy_targets.flush_batch_immediate = &flush_batch_immediate_;
		policy_targets.flush_batch_normal = &flush_batch_normal_;
		policy_targets.reconnect_grace_close_delay_ms = &reconnect_grace_close_delay_ms_;

		dc::cfg::WorldRuntimePolicyDefaults policy_defaults{};
		policy_defaults.default_flush_interval_sec = kDefaultFlushIntervalSec;
		policy_defaults.default_char_ttl_sec = kDefaultCharTtlSec;
		policy_defaults.default_db_pool_size_per_world = 2;
		policy_defaults.default_batch_immediate = kDefaultBatchImmediate;
		policy_defaults.default_batch_normal = kDefaultBatchNormal;
		policy_defaults.default_reconnect_grace_close_delay_ms = kDefaultReconnectGraceCloseDelayMs;

		const auto policy_table = dc::cfg::BuildWorldRuntimeMinPolicyTable(policy_targets, policy_defaults);
		if (!dc::cfg::ApplyMinPolicies(policy_table.int_specs, policy_table.u32_specs, config_fail_fast, &policy_error)) {
			spdlog::error("{}", policy_error);
			return false;
		}
		if (!dc::cfg::ValidateSchemaCompatibility(
			"SYSTEM.CONFIG_SCHEMA_VERSION",
			config_schema_version,
			dc::cfg::kRuntimeConfigSchemaVersion,
			dc::cfg::kRuntimeConfigSchemaMinSupported,
			dc::cfg::kRuntimeConfigSchemaMaxSupported,
			config_fail_fast,
			&policy_error)) {
			spdlog::error("{}", policy_error);
			return false;
		}
		if (config_schema_version < dc::cfg::kRuntimeConfigSchemaMinSupported ||
			config_schema_version > dc::cfg::kRuntimeConfigSchemaMaxSupported) {
			spdlog::warn(
				"[config] schema version unsupported (continue with auto-heal mode). loaded={} supported=[{},{}]",
				config_schema_version,
				dc::cfg::kRuntimeConfigSchemaMinSupported,
				dc::cfg::kRuntimeConfigSchemaMaxSupported);
		}
		else if (config_schema_version != dc::cfg::kRuntimeConfigSchemaVersion) {
			spdlog::warn(
				"[config] schema version mismatch (continue with auto-heal mode). loaded={} expected={} supported=[{},{}]",
				config_schema_version,
				dc::cfg::kRuntimeConfigSchemaVersion,
				dc::cfg::kRuntimeConfigSchemaMinSupported,
				dc::cfg::kRuntimeConfigSchemaMaxSupported);
		}

		// 5) AOI/섹터 sanity
		dc::cfg::NormalizeAoiConfig(g_aoi_ini_cfg);


		spdlog::info("INI loaded (UTF-8): acc='{}', worldset_num={}, recv_buf={}",
			db_acc_, worldset_num_, world_to_log_recv_buffer_size_);

		spdlog::info("INI(DB_WORK): pool_per_world={}, db_shards={}", db_pool_size_per_world_, db_shard_count_);
			spdlog::info("INI(WRITE_BEHIND): flush_interval={}s, batch_immediate={}, batch_normal={}, ttl={}s",
				flush_interval_sec_, flush_batch_immediate_, flush_batch_normal_, char_ttl_sec_);
			spdlog::info("INI(SESSION): reconnect_grace_close_delay_ms={}", reconnect_grace_close_delay_ms_);
			spdlog::info("INI(SYSTEM): config_fail_fast={} schema_version={} expected_schema_version={} supported_schema_range=[{},{}]",
				config_fail_fast,
				config_schema_version,
				dc::cfg::kRuntimeConfigSchemaVersion,
				dc::cfg::kRuntimeConfigSchemaMinSupported,
				dc::cfg::kRuntimeConfigSchemaMaxSupported);
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
			spdlog::debug("[{}] skipped: world_account_handler_ is null", dc::logevt::world::kAccountRouteHeartbeat);
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
		},
				[this](std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldPlayerEnterAck& ack) {
			OnZonePlayerEnterAck(sid, serial, ack);
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
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		if (!world_account_handler_->SendRouteHeartbeat(0, sid, serial)) {
			spdlog::debug("[{}] failed sid={} serial={}", dc::logevt::world::kAccountRouteHeartbeat, sid, serial);
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
		next_account_route_heartbeat_tp_ = std::chrono::steady_clock::now() + dc::k_next_account_route_heartbeat_tp;

		spdlog::info(
			"[{}] sid={} serial={} server_id={} world_id={} channel_id={} zones={} load_score={} flags={} server_name={} public_host={} public_port={}",
			dc::logevt::world::kAccountRouteReady,
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

		spdlog::warn("[{}] sid={} serial={} account route disconnected",
			dc::logevt::world::kAccountRouteDown, sid, serial);
	}

	bool WorldRuntime::NotifyAccountWorldEnterSuccess(
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token)
	{
		if (!account_ready_.load(std::memory_order_acquire)) {
			spdlog::warn(
				"[{}] skipped: account line not ready. account_id={} char_id={}",
				dc::logevt::world::kEnterNotifyRelay,
				account_id,
				char_id);
			return false;
		}

		if (!world_account_handler_) {
			spdlog::warn(
				"[{}] skipped: world_account_handler_ is null. account_id={} char_id={}",
				dc::logevt::world::kEnterNotifyRelay,
				account_id,
				char_id);
			return false;
		}

		const bool sent = world_account_handler_->SendWorldEnterSuccessNotify(
			100,
			account_sid_.load(std::memory_order_relaxed),
			account_serial_.load(std::memory_order_relaxed),
			account_id,
			char_id,
			login_session,
			world_token);

		if (sent) {
			spdlog::info("[{}] relayed account_id={} char_id={} token={} login_session={}",
				dc::logevt::world::kEnterNotifyRelay, account_id, char_id, world_token, login_session);
		} else {
			spdlog::warn("[{}] send failed account_id={} char_id={} token={}",
				dc::logevt::world::kEnterNotifyRelay, account_id, char_id, world_token);
		}

		return sent;
	}
} // namespace svr
