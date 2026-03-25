#include "services/world/runtime/world_runtime_private.h"

namespace svr {

	namespace {
		std::uint64_t MakeWorldCharKey_(std::uint32_t world_code, std::uint64_t char_id)
		{
			return (static_cast<std::uint64_t>(world_code) << 56) ^ char_id;
		}
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
			else if constexpr (std::is_same_v<T, svr::dqs_result::WorldCharacterEnterSnapshotResult>) {
				actor_id = rr.char_id != 0 ? rr.char_id : 0;
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
						spdlog::info("[FlushDirtyChars] world={}, shard={}, pulled={}, saved={}, failed={}, conflicts={}, batch={}, result={}",
							rr.world_code, rr.shard_id, rr.pulled, rr.saved, rr.failed, rr.conflicts, rr.max_batch, (int)rr.result);
						if (rr.conflicts > 0) {
							svr::metrics::g_flush_dirty_conflicts_total.fetch_add(rr.conflicts, std::memory_order_relaxed);
							svr::metrics::g_flush_dirty_conflicted_batches.fetch_add(1, std::memory_order_relaxed);
						}

				// ✅ batch만큼 꽉 찼으면 남은 dirty가 더 있을 확률이 높다
				// - 과도 루프 방지 위해: 결과가 꽉 찬 경우에만 “추가 1회” enqueue
				if (rr.max_batch > 0 && rr.pulled >= rr.max_batch) {
					EnqueueFlushDirtyWorld_(rr.world_code, rr.max_batch);

				}
				return;

			}

				// 3) flush_one_char 결과 처리: 로그/추가 후처리(필요시)는 메인에서만
				if constexpr (std::is_same_v<T, svr::dqs_result::FlushOneCharResult>) {
					spdlog::info(
						"[FlushOneChar] world={}, char_id={}, saved={}, result={}, expected_ver={}, actual_ver={}",
						rr.world_code, rr.char_id, rr.saved, (int)rr.result, rr.expected_version, rr.actual_version);
				if (rr.result == svr::dqs::ResultCode::conflict) {
					spdlog::warn(
						"[FlushOneCharConflict] world={} char_id={} expected_ver={} actual_ver={}",
						rr.world_code, rr.char_id, rr.expected_version, rr.actual_version);
					}
					return;

			}

			if constexpr (std::is_same_v<T, svr::dqs_result::WorldCharacterEnterSnapshotResult>) {
				OnCharacterEnterSnapshotResult_(rr);
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

	std::size_t WorldRuntime::CountInFlightDqs_() const
	{
		std::lock_guard lk(dqs_mtx_);
		std::size_t in_flight = 0;
		for (const auto& slot : dqs_slots_) {
			if (slot.in_use) {
				++in_flight;
			}
		}
		return in_flight;
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
		if (qc == svr::dqs::QueryCase::world_character_enter_snapshot) {
			if (size >= (int)sizeof(svr::dqs_payload::WorldCharacterEnterSnapshot)) {
				svr::dqs_payload::WorldCharacterEnterSnapshot p{};
				std::memcpy(&p, data, sizeof(p));
				return (std::uint32_t)(p.char_id % shard_count);

			}

		}
		return 0;
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
							std::uint32_t conflicts = 0;

								if (world_code >= world_pools_.size() || !world_pools_[world_code] || world_pools_[world_code]->conns.empty())
								{
									slot.result = svr::dqs::ResultCode::db_error;
									svr::dqs_result::FlushDirtyCharsResult r{};
										r.world_code = world_code;
										r.shard_id = shard_id;
										r.max_batch = (std::uint32_t)max_batch;
										r.pulled = 0; r.saved = 0; r.failed = 0; r.conflicts = 0;
										r.result = slot.result;
										PostDqsResult(std::move(r));
									break;
							}

							auto& conn = world_pools_[world_code]->next();

								auto ids = redis_cache_->take_dirty_batch(world_code, shard_id, max_batch);
								pulled = (std::uint32_t)ids.size();
								bool conflict_logged = false;
									for (auto char_id : ids)
									{
									auto blob = redis_cache_->get_character_blob(world_code, char_id);
									if (!blob)
										continue;

									std::uint32_t actual_version = 0;
									svr::demo::DemoCharState parsed{};
									if (svr::demo::TryDeserializeDemo(*blob, parsed) && parsed.char_id == char_id) {
										actual_version = parsed.version;
									}
									const auto expected_version = TryGetExpectedCharVersion_(world_code, char_id);
										if (expected_version != 0 &&
											actual_version != 0 &&
											actual_version != expected_version) {
											++conflicts;
											if (!conflict_logged) {
												conflict_logged = true;
												svr::metrics::g_flush_dirty_conflict_world_sample.store(world_code, std::memory_order_relaxed);
												svr::metrics::g_flush_dirty_conflict_shard_sample.store(shard_id, std::memory_order_relaxed);
												svr::metrics::g_flush_dirty_conflict_char_sample.store(char_id, std::memory_order_relaxed);
												spdlog::warn(
													"[FlushDirtyCharsConflict] world={} shard={} char_id={} expected_ver={} actual_ver={}",
													world_code,
													shard_id,
													char_id,
													expected_version,
													actual_version);
											}
											redis_cache_->mark_dirty(world_code, char_id);
											slot.result = svr::dqs::ResultCode::conflict;
											continue;
									}

									try
									{
										db::save_character_blob(conn, char_id, *blob);
										EraseExpectedCharVersion_(world_code, char_id);
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
								r.shard_id = shard_id;
								r.max_batch = (std::uint32_t)max_batch;
								r.pulled = pulled;
								r.saved = saved;
								r.failed = failed;
								r.conflicts = conflicts;
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
							const std::uint32_t expected_version = payload.expected_version;
							bool saved = false;

							if (world_code >= world_pools_.size() || !world_pools_[world_code] || world_pools_[world_code]->conns.empty())
							{
								slot.result = svr::dqs::ResultCode::db_error;
								svr::dqs_result::FlushOneCharResult r{};
								r.world_code = world_code;
								r.char_id = char_id;
								r.expected_version = expected_version;
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
								r.expected_version = expected_version;
								r.saved = false;
								r.result = slot.result;
								PostDqsResult(std::move(r));
								break;
							}

							std::uint32_t actual_version = 0;
							svr::demo::DemoCharState parsed{};
							if (svr::demo::TryDeserializeDemo(*blob, parsed) && parsed.char_id == char_id) {
								actual_version = parsed.version;
							}

							if (expected_version != 0 &&
								actual_version != 0 &&
								actual_version != expected_version) {
								slot.result = svr::dqs::ResultCode::conflict;
								svr::dqs_result::FlushOneCharResult r{};
								r.world_code = world_code;
								r.char_id = char_id;
								r.expected_version = expected_version;
								r.actual_version = actual_version;
								r.saved = false;
								r.result = slot.result;
								PostDqsResult(std::move(r));
								break;
							}

							try
							{
								auto& conn = world_pools_[world_code]->next();
								svr::demo::DemoCharState loaded_demo{};
								CharacterRuntimeHotState hot_state{};
								if (svr::demo::TryDeserializeDemo(*blob, loaded_demo) && loaded_demo.char_id == char_id) {
									hot_state.resources.gold = loaded_demo.gold;
									hot_state.version = loaded_demo.version;
								}
								WorldCharacterRepository::FlushCharacterHotState(conn, world_code, char_id, hot_state, *blob);
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
							r.expected_version = expected_version;
							r.actual_version = actual_version;
							r.saved = saved;
							r.result = slot.result;
							PostDqsResult(std::move(r));
						}
						break;
					case svr::dqs::QueryCase::world_character_enter_snapshot:
						{
							if (slot.data_size < sizeof(svr::dqs_payload::WorldCharacterEnterSnapshot))
							{
								slot.result = svr::dqs::ResultCode::invalid_data;
								break;
							}

							svr::dqs_payload::WorldCharacterEnterSnapshot payload{};
							std::memcpy(&payload, slot.data.data(), sizeof(payload));

							svr::dqs_result::WorldCharacterEnterSnapshotResult r{};
							r.world_code = payload.world_code;
							r.sid = payload.sid;
							r.serial = payload.serial;
							r.trace_id = payload.trace_id;
							r.request_id = payload.request_id;
							r.account_id = payload.account_id;
							r.char_id = payload.char_id;

							if (payload.world_code >= world_pools_.size() || !world_pools_[payload.world_code] || world_pools_[payload.world_code]->conns.empty())
							{
								slot.result = svr::dqs::ResultCode::db_error;
								std::memcpy(r.fail_reason, "world_db_not_ready", sizeof("world_db_not_ready"));
								r.result = slot.result;
								PostDqsResult(std::move(r));
								break;
							}

							std::string cached_blob;
							if (payload.cached_state_blob_size > 0) {
								const auto size = static_cast<std::size_t>(std::min<std::uint16_t>(
									payload.cached_state_blob_size,
									static_cast<std::uint16_t>(dc::k_character_state_blob_max_len)));
								cached_blob.assign(payload.cached_state_blob, payload.cached_state_blob + size);
							}

							try
							{
								auto& conn = world_pools_[payload.world_code]->next();
								auto loaded = WorldCharacterRepository::LoadCharacterEnterSnapshot(
									conn,
									payload.account_id,
									payload.char_id,
									cached_blob);

								r.found = loaded.found;
								r.cache_blob_applied = loaded.cache_blob_applied;
								r.core_state = std::move(loaded.core_state);
								if (!loaded.fail_reason.empty()) {
									std::memcpy(
										r.fail_reason,
										loaded.fail_reason.c_str(),
										std::min<std::size_t>(loaded.fail_reason.size() + 1, sizeof(r.fail_reason)));
								}
							}
							catch (const std::exception& e)
							{
								spdlog::error("LoadCharacterEnterSnapshot DQS fail char_id={} err={}", payload.char_id, e.what());
								slot.result = svr::dqs::ResultCode::db_error;
								std::memcpy(r.fail_reason, "db_error", sizeof("db_error"));
							}

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

		svr::demo::DemoCharState st{};
		if (svr::demo::TryDeserializeDemo(blob, st) && st.char_id == char_id) {
			UpdateExpectedCharVersion_(world_code, char_id, st.version);
		}

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
		payload.expected_version = 0;

		auto& actor = GetOrCreatePlayerActor(char_id);
		if (const auto* core = actor.CoreState(); core != nullptr) {
			const auto blob = actor.SerializePersistentState();
			CacheCharacterState(world_code, char_id, blob);
			payload.expected_version = core->hot.version;
			spdlog::debug(
				"RequestFlushCharacter uses core_state.hot. char_id={} version={} dirty_flags={}",
				char_id,
				core->hot.version,
				static_cast<std::uint32_t>(core->hot.dirty_flags));
		}

		if (auto blob = TryLoadCharacterState(world_code, char_id)) {
			svr::demo::DemoCharState st{};
			if (svr::demo::TryDeserializeDemo(*blob, st) && st.char_id == char_id) {
				payload.expected_version = st.version;
			}
		}

		(void)PushDQSData(
			(std::uint8_t)svr::dqs::ProcessCode::world,
			(std::uint8_t)svr::dqs::QueryCase::flush_one_char,
			reinterpret_cast<const char*>(&payload),
			(int)sizeof(payload));
	}

	void WorldRuntime::UpdateExpectedCharVersion_(
		std::uint32_t world_code,
		std::uint64_t char_id,
		std::uint32_t version)
	{
		if (char_id == 0 || version == 0) {
			return;
		}
		std::lock_guard lk(expected_char_ver_mtx_);
		expected_char_version_by_key_[MakeWorldCharKey_(world_code, char_id)] = version;
	}

	std::uint32_t WorldRuntime::TryGetExpectedCharVersion_(
		std::uint32_t world_code,
		std::uint64_t char_id) const
	{
		if (char_id == 0) {
			return 0;
		}
		std::lock_guard lk(expected_char_ver_mtx_);
		const auto it = expected_char_version_by_key_.find(MakeWorldCharKey_(world_code, char_id));
		if (it == expected_char_version_by_key_.end()) {
			return 0;
		}
		return it->second;
	}

	void WorldRuntime::EraseExpectedCharVersion_(
		std::uint32_t world_code,
		std::uint64_t char_id)
	{
		if (char_id == 0) {
			return;
		}
		std::lock_guard lk(expected_char_ver_mtx_);
		expected_char_version_by_key_.erase(MakeWorldCharKey_(world_code, char_id));
	}


} // namespace svr
