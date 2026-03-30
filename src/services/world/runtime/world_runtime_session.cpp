#include "services/world/runtime/world_runtime_private.h"
#include "services/world/common/string_utils.h"
#include "services/world/metrics/world_metrics.h"
#include "server_common/log/enter_flow_log.h"
#include "server_common/session/session_key.h"
#include <array>
#include <random>

namespace {

	std::string GenerateReconnectToken_()
	{
		constexpr char kHex[] = "0123456789ABCDEF";
		std::array<std::uint8_t, 32> bytes{};

		std::random_device rd;
		std::mt19937_64 gen(rd());

		for (auto& b : bytes) {
			b = static_cast<std::uint8_t>(gen() & 0xff);
		}

		std::string out;
		out.resize(bytes.size() * 2);
		for (std::size_t i = 0; i < bytes.size(); ++i) {
			out[i * 2 + 0] = kHex[(bytes[i] >> 4) & 0x0f];
			out[i * 2 + 1] = kHex[bytes[i] & 0x0f];
		}
		return out;
	}

} // namespace

namespace svr {

	void WorldRuntime::ErasePendingEnterWorldConsumeRequestsBySession_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		std::size_t erased = 0;
		{
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			for (auto it = pending_enter_world_consume_.begin(); it != pending_enter_world_consume_.end();) {
				if (it->second.sid == sid && it->second.serial == serial) {
					it = pending_enter_world_consume_.erase(it);
					++erased;
				}
				else {
					++it;
				}
			}
		}

		if (erased != 0) {
			spdlog::info(
				"ErasePendingEnterWorldConsumeRequestsBySession_ removed pending consume requests. sid={} serial={} erased={}",
				sid,
				serial,
				erased);
		}
	}

	void WorldRuntime::ErasePendingEnterWorldFinalizeBySession_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		std::size_t erased = 0;
		for (auto it = pending_enter_world_finalize_by_assign_request_.begin();
			it != pending_enter_world_finalize_by_assign_request_.end();) {
			auto& list = it->second;
			const auto before = list.size();
			list.erase(
				std::remove_if(
					list.begin(),
					list.end(),
					[sid, serial](const PendingEnterWorldFinalize& e) {
				return e.enter_pending.sid == sid && e.enter_pending.serial == serial;
			}),
				list.end());

			erased += (before - list.size());
			if (list.empty()) {
				auto pending_assign_it = pending_zone_assign_requests_.find(it->first);
				if (pending_assign_it != pending_zone_assign_requests_.end()) {
					pending_zone_assign_request_id_by_map_key_.erase(pending_assign_it->second.map_key);
					pending_zone_assign_requests_.erase(pending_assign_it);
				}

				spdlog::info(
					"ErasePendingEnterWorldFinalizeBySession_ removed orphan pending zone-assign request. sid={} serial={} request_id={}",
					sid,
					serial,
					it->first);
				it = pending_enter_world_finalize_by_assign_request_.erase(it);
			}
			else {
				++it;
			}
		}

		if (erased != 0) {
			spdlog::info(
				"ErasePendingEnterWorldFinalizeBySession_ removed pending finalize entries. sid={} serial={} erased={}",
				sid,
				serial,
				erased);
		}
	}

	void WorldRuntime::ErasePendingCharacterEnterSnapshotRequestsBySession_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		std::size_t erased = 0;
		for (auto it = pending_character_enter_snapshot_requests_.begin();
			it != pending_character_enter_snapshot_requests_.end();) {
			if (it->second.enter_pending.sid == sid &&
				it->second.enter_pending.serial == serial) {
				it = pending_character_enter_snapshot_requests_.erase(it);
				++erased;
			}
			else {
				++it;
			}
		}

		if (erased != 0) {
			spdlog::info(
				"ErasePendingCharacterEnterSnapshotRequestsBySession_ removed pending snapshot requests. sid={} serial={} erased={}",
				sid,
				serial,
				erased);
		}
	}

	void WorldRuntime::ErasePendingZonePlayerEnterRequestsBySession_(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		std::size_t erased = 0;
		for (auto it = pending_zone_player_enter_requests_.begin();
			it != pending_zone_player_enter_requests_.end();) {
			if (it->second.enter_pending.sid == sid &&
				it->second.enter_pending.serial == serial) {
				it = pending_zone_player_enter_requests_.erase(it);
				++erased;
			}
			else {
				++it;
			}
		}

		if (erased != 0) {
			spdlog::info(
				"ErasePendingZonePlayerEnterRequestsBySession_ removed pending zone-player-enter entries. sid={} serial={} erased={}",
				sid,
				serial,
				erased);
		}
	}

	void WorldRuntime::AbortEnterWorldFlowBySession_(
		std::uint32_t sid,
		std::uint32_t serial,
		std::string_view log_text)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		std::optional<dc::enterlog::EnterTraceContext> log_ctx;
		{
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			for (const auto& [_, pending] : pending_enter_world_consume_) {
				if (pending.sid == sid && pending.serial == serial) {
					log_ctx = dc::enterlog::EnterTraceContext{
						pending.trace_id,
						pending.account_id,
						pending.char_id,
						pending.sid,
						pending.serial,
						pending.login_session,
						pending.world_token
					};
					break;
				}
			}
		}
		if (!log_ctx.has_value()) {
			for (const auto& [_, list] : pending_enter_world_finalize_by_assign_request_) {
				for (const auto& finalize : list) {
					const auto& pending = finalize.enter_pending;
					if (pending.sid == sid && pending.serial == serial) {
						log_ctx = dc::enterlog::EnterTraceContext{
							pending.trace_id,
							pending.account_id,
							pending.char_id,
							pending.sid,
							pending.serial,
							pending.login_session,
							pending.world_token
						};
						break;
					}
				}
				if (log_ctx.has_value()) {
					break;
				}
			}
		}
		if (!log_ctx.has_value()) {
			for (const auto& [_, pending_snapshot] : pending_character_enter_snapshot_requests_) {
				const auto& pending = pending_snapshot.enter_pending;
				if (pending.sid == sid && pending.serial == serial) {
					log_ctx = dc::enterlog::EnterTraceContext{
						pending.trace_id,
						pending.account_id,
						pending.char_id,
						pending.sid,
						pending.serial,
						pending.login_session,
						pending.world_token
					};
					break;
				}
			}
		}
		if (!log_ctx.has_value()) {
			for (const auto& [_, pending_enter] : pending_zone_player_enter_requests_) {
				const auto& pending = pending_enter.enter_pending;
				if (pending.sid == sid && pending.serial == serial) {
					log_ctx = dc::enterlog::EnterTraceContext{
						pending.trace_id,
						pending.account_id,
						pending.char_id,
						pending.sid,
						pending.serial,
						pending.login_session,
						pending.world_token
					};
					break;
				}
			}
		}

		{
			const auto session_key = dc::PackSessionKey(sid, serial);
			std::lock_guard lk(world_session_mtx_);
			world_enter_stage_by_session_key_[session_key] = WorldEnterStage::Closing;

			for (auto it = pending_enter_session_key_by_char_id_.begin();
				it != pending_enter_session_key_by_char_id_.end();) {
				if (it->second == session_key) {
					it = pending_enter_session_key_by_char_id_.erase(it);
				}
				else {
					++it;
				}
			}
		}

		ErasePendingEnterWorldConsumeRequestsBySession_(sid, serial);
		ErasePendingEnterWorldFinalizeBySession_(sid, serial);
		ErasePendingCharacterEnterSnapshotRequestsBySession_(sid, serial);
		ErasePendingZonePlayerEnterRequestsBySession_(sid, serial);

		spdlog::info(
			"{} sid={} serial={}",
			log_text,
			sid,
			serial);
		if (log_ctx.has_value()) {
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				*log_ctx,
				log_text);
		}
	}

	BeginEnterWorldSessionResult WorldRuntime::TryBeginEnterWorldSession(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t account_id,
		std::uint64_t char_id)
	{
		BeginEnterWorldSessionResult result{};
		if (!dc::IsValidSessionKey(sid, serial) || account_id == 0 || char_id == 0) {
			result.kind = BeginEnterWorldSessionResultKind::InvalidInput;
			return result;
		}

		const auto session_key = dc::PackSessionKey(sid, serial);
		std::lock_guard lk(world_session_mtx_);

		if (auto it = world_enter_stage_by_session_key_.find(session_key); it != world_enter_stage_by_session_key_.end()) {
			result.stage = it->second;
			switch (it->second) {
			case WorldEnterStage::EnterPending:
				result.kind = BeginEnterWorldSessionResultKind::AlreadyPending;
				return result;
			case WorldEnterStage::InWorld:
				result.kind = BeginEnterWorldSessionResultKind::AlreadyInWorld;
				return result;
			case WorldEnterStage::Closing:
				result.kind = BeginEnterWorldSessionResultKind::Closing;
				return result;
			case WorldEnterStage::None:
			default:
				break;
			}
		}

		if (auto pending_it = pending_enter_session_key_by_char_id_.find(char_id);
			pending_it != pending_enter_session_key_by_char_id_.end() && pending_it->second != session_key) {
			result.kind = BeginEnterWorldSessionResultKind::AlreadyPending;
			result.stage = WorldEnterStage::EnterPending;
			return result;
		}

		world_enter_stage_by_session_key_[session_key] = WorldEnterStage::EnterPending;
		pending_enter_session_key_by_char_id_[char_id] = session_key;
		result.kind = BeginEnterWorldSessionResultKind::Started;
		result.stage = WorldEnterStage::EnterPending;
		return result;
	}

	void WorldRuntime::CancelPendingEnterWorldSession(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		const auto session_key = dc::PackSessionKey(sid, serial);
		std::lock_guard lk(world_session_mtx_);
		auto it = world_enter_stage_by_session_key_.find(session_key);
		if (it != world_enter_stage_by_session_key_.end() && it->second == WorldEnterStage::EnterPending) {
			world_enter_stage_by_session_key_.erase(it);
		}

		if (char_id != 0) {
			auto pending_it = pending_enter_session_key_by_char_id_.find(char_id);
			if (pending_it != pending_enter_session_key_by_char_id_.end() && pending_it->second == session_key) {
				pending_enter_session_key_by_char_id_.erase(pending_it);
			}
		}
	}

	bool WorldRuntime::IsEnterWorldSessionPending(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id) const
	{
		if (!dc::IsValidSessionKey(sid, serial) || char_id == 0) {
			return false;
		}

		const auto session_key = dc::PackSessionKey(sid, serial);
		std::lock_guard lk(world_session_mtx_);
		auto st_it = world_enter_stage_by_session_key_.find(session_key);
		if (st_it == world_enter_stage_by_session_key_.end() || st_it->second != WorldEnterStage::EnterPending) {
			return false;
		}

		auto pending_it = pending_enter_session_key_by_char_id_.find(char_id);
		return pending_it != pending_enter_session_key_by_char_id_.end() && pending_it->second == session_key;
	}

	bool WorldRuntime::PromoteEnterWorldSessionToInWorld(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id)
	{
		if (!dc::IsValidSessionKey(sid, serial) || char_id == 0) {
			return false;
		}

		const auto session_key = dc::PackSessionKey(sid, serial);
		std::lock_guard lk(world_session_mtx_);
		auto st_it = world_enter_stage_by_session_key_.find(session_key);
		if (st_it == world_enter_stage_by_session_key_.end() || st_it->second != WorldEnterStage::EnterPending) {
			return false;
		}

		auto session_it = authed_sessions_by_sid_.find(sid);
		if (session_it == authed_sessions_by_sid_.end() || session_it->second.serial != serial || session_it->second.char_id != char_id) {
			return false;
		}

		auto pending_it = pending_enter_session_key_by_char_id_.find(char_id);
		if (pending_it == pending_enter_session_key_by_char_id_.end() || pending_it->second != session_key) {
			return false;
		}

		pending_enter_session_key_by_char_id_.erase(pending_it);
		st_it->second = WorldEnterStage::InWorld;
		return true;
	}

	void WorldRuntime::MarkEnterWorldSessionClosing(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		const auto session_key = dc::PackSessionKey(sid, serial);
		std::lock_guard lk(world_session_mtx_);
		world_enter_stage_by_session_key_[session_key] = WorldEnterStage::Closing;

		auto authed_it = authed_sessions_by_sid_.find(sid);
		if (authed_it != authed_sessions_by_sid_.end() && authed_it->second.serial == serial) {
			auto pending_it = pending_enter_session_key_by_char_id_.find(authed_it->second.char_id);
			if (pending_it != pending_enter_session_key_by_char_id_.end() && pending_it->second == session_key) {
				pending_enter_session_key_by_char_id_.erase(pending_it);
			}
		}
	}
	bool WorldRuntime::TryReserveDelayedWorldClose_(
		std::uint32_t sid,
		std::uint32_t serial) noexcept
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
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
		if (!dc::IsValidSessionKey(sid, serial)) {
			return false;
		}

		const DelayedCloseKey key{ sid, serial };
		auto timer = std::make_shared<boost::asio::steady_timer>(io_);
		timer->expires_after(delay);
		WorldSessionCloseReason close_reason = WorldSessionCloseReason::NetworkDisconnect;

		{
			const auto session_key = dc::PackSessionKey(sid, serial);
			std::lock_guard lk(world_session_mtx_);
			if (auto it = session_close_reason_by_session_key_.find(session_key);
				it != session_close_reason_by_session_key_.end()) {
				close_reason = it->second;
			}
		}

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
			it->second.log_ctx.close_reason = close_reason;
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
		if (!dc::IsValidSessionKey(sid, serial)) {
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
		if (!dc::IsValidSessionKey(sid, serial)) {
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

	void WorldRuntime::MarkWorldSessionCloseReason(
		std::uint32_t sid,
		std::uint32_t serial,
		WorldSessionCloseReason reason)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		const auto session_key = dc::PackSessionKey(sid, serial);
		std::lock_guard lk(world_session_mtx_);
		session_close_reason_by_session_key_[session_key] = reason;
	}

	std::optional<WorldAuthedSession> WorldRuntime::TryGetAuthenticatedWorldSession(
		std::uint32_t sid,
		std::uint32_t serial) const
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return std::nullopt;
		}

		auto current = FindAuthenticatedWorldSessionBySid_(sid);
		if (!current.has_value() || current->serial != serial) {
			return std::nullopt;
		}

		return current;
	}

	std::string WorldRuntime::GetOrCreateReconnectTokenForSession(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		if (!dc::IsValidSessionKey(sid, serial)) {
			return {};
		}

		std::lock_guard lk(world_session_mtx_);
		auto it = authed_sessions_by_sid_.find(sid);
		if (it == authed_sessions_by_sid_.end() || it->second.serial != serial) {
			return {};
		}

		if (it->second.reconnect_token.empty()) {
			it->second.reconnect_token = GenerateReconnectToken_();
		}

		reconnect_session_key_by_token_[it->second.reconnect_token] =
			dc::PackSessionKey(sid, serial);
		return it->second.reconnect_token;
	}

	ReconnectWorldSessionResult WorldRuntime::TryReconnectWorldSession(
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view reconnect_token,
		std::uint32_t sid,
		std::uint32_t serial)
	{
		ReconnectWorldSessionResult result{};
		result.code = pt_w::ReconnectWorldResultCode::internal_error;
		result.current_session.account_id = account_id;
		result.current_session.char_id = char_id;
		result.current_session.sid = sid;
		result.current_session.serial = serial;

		if (!dc::IsValidSessionKey(sid, serial) || account_id == 0 || char_id == 0 || reconnect_token.empty()) {
			return result;
		}

		std::uint32_t previous_sid = 0;
		std::uint32_t previous_serial = 0;

		{
			std::lock_guard lk(world_session_mtx_);

			const auto token_it = reconnect_session_key_by_token_.find(std::string(reconnect_token));
			if (token_it == reconnect_session_key_by_token_.end()) {
				result.code = pt_w::ReconnectWorldResultCode::token_not_found;
				return result;
			}

			const auto previous_session_key = token_it->second;
			previous_sid = dc::UnpackSessionSid(previous_session_key);
			previous_serial = dc::UnpackSessionSerial(previous_session_key);

			auto previous_it = authed_sessions_by_sid_.find(previous_sid);
			if (previous_it == authed_sessions_by_sid_.end() ||
				previous_it->second.serial != previous_serial) {
				reconnect_session_key_by_token_.erase(token_it);
				result.code = pt_w::ReconnectWorldResultCode::session_expired;
				return result;
			}

			result.previous_session = previous_it->second;

			if (previous_it->second.account_id != account_id) {
				result.code = pt_w::ReconnectWorldResultCode::account_mismatch;
				return result;
			}

			if (previous_it->second.char_id != char_id) {
				result.code = pt_w::ReconnectWorldResultCode::char_mismatch;
				return result;
			}

			if (previous_it->second.reconnect_token != reconnect_token) {
				result.code = pt_w::ReconnectWorldResultCode::token_not_found;
				return result;
			}

			WorldAuthedSession replacement = previous_it->second;
			replacement.sid = sid;
			replacement.serial = serial;

			authed_sessions_by_sid_.erase(previous_it);
			authed_sessions_by_sid_[sid] = replacement;
			authed_session_key_by_char_id_[char_id] = dc::PackSessionKey(sid, serial);
			authed_session_key_by_account_id_[account_id] = dc::PackSessionKey(sid, serial);
			reconnect_session_key_by_token_[replacement.reconnect_token] =
				dc::PackSessionKey(sid, serial);

			const auto old_key = dc::PackSessionKey(previous_sid, previous_serial);
			const auto new_key = dc::PackSessionKey(sid, serial);

			world_enter_stage_by_session_key_.erase(old_key);
			world_enter_stage_by_session_key_[new_key] = WorldEnterStage::InWorld;
			session_close_reason_by_session_key_.erase(old_key);
			session_close_reason_by_session_key_.erase(new_key);

			for (auto pending_it = pending_enter_session_key_by_char_id_.begin();
				pending_it != pending_enter_session_key_by_char_id_.end();) {
				if (pending_it->second == old_key) {
					pending_it = pending_enter_session_key_by_char_id_.erase(pending_it);
				}
				else {
					++pending_it;
				}
			}

			result.current_session = replacement;
			result.reconnect_token = replacement.reconnect_token;
		}

		CancelDelayedWorldClose(previous_sid, previous_serial);

		auto& actor = GetOrCreatePlayerActor(char_id);
		actor.bind_session(sid, serial);
		result.zone_id = actor.GetZoneId();
		result.map_id = actor.GetMapId();
		result.instance_id = actor.GetMapInstanceId();
		const auto pos = actor.GetPosition();
		result.x = pos.x;
		result.y = pos.y;
		result.code = pt_w::ReconnectWorldResultCode::success;

		spdlog::info(
			"World reconnect succeeded. account_id={} char_id={} old_sid={} old_serial={} new_sid={} new_serial={} zone_id={} map_id={} instance_id={}",
			account_id,
			char_id,
			previous_sid,
			previous_serial,
			sid,
			serial,
			result.zone_id,
			result.map_id,
			result.instance_id);

		return result;
	}

	void WorldRuntime::HandleWorldSessionClosed(
		std::uint32_t sid,
		std::uint32_t serial)
	{
		AbortEnterWorldFlowBySession_(
			sid,
			serial,
			"HandleWorldSessionClosed aborted pending enter-world flow before close processing.");

		MarkEnterWorldSessionClosing(sid, serial);
		{
			const auto session_key = dc::PackSessionKey(sid, serial);
			std::lock_guard lk(world_session_mtx_);
			world_enter_stage_by_session_key_.erase(session_key);
		}

		std::uint64_t close_char_id = 0;
		WorldSessionCloseReason close_reason = WorldSessionCloseReason::NetworkDisconnect;
		{
			const auto session_key = dc::PackSessionKey(sid, serial);
			std::lock_guard lk(world_session_mtx_);
			if (auto reason_it = session_close_reason_by_session_key_.find(session_key);
				reason_it != session_close_reason_by_session_key_.end()) {
				close_reason = reason_it->second;
			}
		}
		if (const auto current = FindAuthenticatedWorldSessionBySid_(sid); current.has_value()) {
			close_char_id = current->char_id;
		}

		boost::asio::dispatch(
			duplicate_session_strand_,
			[this, sid, serial, close_char_id, close_reason]() {
			const bool allow_grace =
				close_reason != WorldSessionCloseReason::ExplicitSoftLogout &&
				close_reason != WorldSessionCloseReason::ExplicitHardLogout;

			if (allow_grace && TryReserveDelayedWorldClose_(sid, serial)) {
				if (ArmReservedDelayedWorldClose_(
					sid,
					serial,
					std::chrono::milliseconds(reconnect_grace_close_delay_ms_),
					0,
					close_char_id))
				{
					spdlog::info(
						"[session_close] reconnect grace close armed. char_id={} sid={} serial={} delay_ms={}",
						close_char_id,
						sid,
						serial,
						reconnect_grace_close_delay_ms_);
					return;
				}

				DelayedCloseEntry released_entry{};
				ReleaseDelayedWorldCloseReservation_(sid, serial, &released_entry);
			}

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

	void WorldRuntime::BeginLeaveWorld_(
		const LeaveWorldContext& ctx,
		std::string_view reason_log)
	{
		if (ctx.char_id == 0) {
			return;
		}

		PostActor(
			ctx.char_id,
			[this, ctx, reason = std::string(reason_log)]() {
			auto& a = GetOrCreatePlayerActor(ctx.char_id);

			if (dc::IsValidSessionKey(ctx.sid, ctx.serial)) {
				a.unbind_session(ctx.sid, ctx.serial);
			}

			if (ctx.zone_id != 0) {
				SendZonePlayerLeave_(ctx.zone_id, ctx.char_id, ctx.map_template_id, ctx.instance_id);
				PostActor(
					svr::MakeZoneActorId(ctx.zone_id),
					[this, char_id = ctx.char_id, zone_id = ctx.zone_id]() {
					auto& z = GetOrCreateZoneActor(zone_id);
					z.Leave(char_id);
				});
			}

			if (static_cast<std::uint16_t>(a.GetZoneId()) == ctx.zone_id &&
				a.GetMapId() == ctx.map_template_id &&
				a.GetMapInstanceId() == ctx.instance_id) {
				a.ClearWorldPosition();
			}

			spdlog::info(
				"{} char_id={} sid={} serial={} zone_id={} map_template_id={} instance_id={}",
				reason,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				ctx.zone_id,
				ctx.map_template_id,
				ctx.instance_id);
		});
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
			const LeaveWorldContext leave_ctx{
				.char_id = close_char_id,
				.sid = close_sid,
				.serial = close_serial,
				.zone_id = static_cast<std::uint16_t>(a.GetZoneId()),
				.map_template_id = a.GetMapId(),
				.instance_id = a.GetMapInstanceId(),
			};
			BeginLeaveWorld_(leave_ctx, "CleanupClosedWorldSessionActors_ started leave cleanup.");
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
		if (!dc::IsValidSessionKey(sid, serial)) {
			return;
		}

		DelayedCloseEntry released_entry{};
		const bool released = ReleaseDelayedWorldCloseReservation_(
			sid,
			serial,
			&released_entry);

		WorldSessionCloseReason close_reason = WorldSessionCloseReason::NetworkDisconnect;
		{
			const auto session_key = dc::PackSessionKey(sid, serial);
			std::lock_guard lk(world_session_mtx_);
			if (auto it = session_close_reason_by_session_key_.find(session_key);
				it != session_close_reason_by_session_key_.end()) {
				close_reason = it->second;
			}
		}

		const auto unbind_result =
			UnbindAuthenticatedWorldSessionBySid(sid, serial);

		ClosedAuthedSessionContext closed_ctx =
			MakeClosedAuthedSessionContext_(unbind_result, sid, serial);
		closed_ctx.close_reason = close_reason;

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
		if (ctx.char_id == 0 || !dc::IsValidSessionKey(ctx.sid, ctx.serial)) {
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
			serial,
			{}
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
					auto char_it = authed_session_key_by_char_id_.find(victim.char_id);
					if (char_it != authed_session_key_by_char_id_.end() &&
						dc::MatchesPackedSessionKey(char_it->second, victim.sid, victim.serial)) {
						authed_session_key_by_char_id_.erase(char_it);
					}
				}

				if (victim.account_id != 0) {
					auto account_it = authed_session_key_by_account_id_.find(victim.account_id);
					if (account_it != authed_session_key_by_account_id_.end() &&
						dc::MatchesPackedSessionKey(account_it->second, victim.sid, victim.serial)) {
						authed_session_key_by_account_id_.erase(account_it);
					}
				}

				if (!victim.reconnect_token.empty()) {
					auto token_it = reconnect_session_key_by_token_.find(victim.reconnect_token);
					if (token_it != reconnect_session_key_by_token_.end() &&
						dc::MatchesPackedSessionKey(token_it->second, victim.sid, victim.serial)) {
						reconnect_session_key_by_token_.erase(token_it);
					}
				}

				session_close_reason_by_session_key_.erase(dc::PackSessionKey(victim.sid, victim.serial));
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
				auto char_sid_it = authed_session_key_by_char_id_.find(char_id);
				if (char_sid_it != authed_session_key_by_char_id_.end()) {
					auto old_it = authed_sessions_by_sid_.find(dc::UnpackSessionSid(char_sid_it->second));
					if (old_it != authed_sessions_by_sid_.end()) {
						const auto expected_serial = dc::UnpackSessionSerial(char_sid_it->second);
						if (old_it->second.serial == expected_serial) {
							result.old_char_session = old_it->second;
						}
					}
				}

				auto account_sid_it = authed_session_key_by_account_id_.find(account_id);
				if (account_sid_it != authed_session_key_by_account_id_.end()) {
					auto old_it = authed_sessions_by_sid_.find(dc::UnpackSessionSid(account_sid_it->second));
					if (old_it != authed_sessions_by_sid_.end()) {
						const auto expected_serial = dc::UnpackSessionSerial(account_sid_it->second);
						if (old_it->second.serial == expected_serial) {
							result.old_account_session = old_it->second;
						}
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

				if (result.current_session.reconnect_token.empty()) {
					if (!result.old_char_session.reconnect_token.empty()) {
						result.current_session.reconnect_token = result.old_char_session.reconnect_token;
					}
					else {
						result.current_session.reconnect_token = GenerateReconnectToken_();
					}
				}

				authed_sessions_by_sid_[sid] = result.current_session;
				authed_session_key_by_char_id_[char_id] = dc::PackSessionKey(sid, serial);
				authed_session_key_by_account_id_[account_id] = dc::PackSessionKey(sid, serial);
				reconnect_session_key_by_token_[result.current_session.reconnect_token] =
					dc::PackSessionKey(sid, serial);
				session_close_reason_by_session_key_.erase(dc::PackSessionKey(sid, serial));

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

		if (!dc::IsValidSessionKey(sid, serial)) {
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
				auto char_it = authed_session_key_by_char_id_.find(result.session.char_id);
				if (char_it != authed_session_key_by_char_id_.end() &&
					dc::MatchesPackedSessionKey(char_it->second, sid, serial)) {
					authed_session_key_by_char_id_.erase(char_it);
				}
			}

			if (result.session.account_id != 0) {
				auto account_it = authed_session_key_by_account_id_.find(result.session.account_id);
				if (account_it != authed_session_key_by_account_id_.end() &&
					dc::MatchesPackedSessionKey(account_it->second, sid, serial)) {
					authed_session_key_by_account_id_.erase(account_it);
				}
			}

			const auto session_key = dc::PackSessionKey(sid, serial);
			authed_sessions_by_sid_.erase(it);
			if (!result.session.reconnect_token.empty()) {
				auto token_it = reconnect_session_key_by_token_.find(result.session.reconnect_token);
				if (token_it != reconnect_session_key_by_token_.end() && token_it->second == session_key) {
					reconnect_session_key_by_token_.erase(token_it);
				}
			}
			session_close_reason_by_session_key_.erase(session_key);
			world_enter_stage_by_session_key_.erase(session_key);
			for (auto pending_it = pending_enter_session_key_by_char_id_.begin();
				pending_it != pending_enter_session_key_by_char_id_.end();) {
				if (pending_it->second == session_key) {
					pending_it = pending_enter_session_key_by_char_id_.erase(pending_it);
				}
				else {
					++pending_it;
				}
			}
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

		auto sid_it = authed_session_key_by_char_id_.find(char_id);
		if (sid_it == authed_session_key_by_char_id_.end()) {
			return std::nullopt;
		}

		auto session_it = authed_sessions_by_sid_.find(dc::UnpackSessionSid(sid_it->second));
		if (session_it == authed_sessions_by_sid_.end()) {
			return std::nullopt;
		}
		if (session_it->second.serial != dc::UnpackSessionSerial(sid_it->second)) {
			return std::nullopt;
		}

		return session_it->second;
	}



	SessionCloseLogContext
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
		ctx.close_reason = closed_ctx.close_reason;
		return ctx;
	}



	ClosedAuthedSessionContext
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



	SessionCloseLogContext
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

	void WorldRuntime::EnqueueDuplicateWorldSessionKickClose_(
		DuplicateLoginLogContext ctx)
	{
		if (ctx.char_id == 0 || !dc::IsValidSessionKey(ctx.sid, ctx.serial)) {
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
		if (!dc::IsValidSessionKey(victim.sid, victim.serial)) {
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

		auto* world_handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		const auto victim_remote = world_handler ? world_handler->GetLatestRemoteEndpointForRuntime(victim.sid) : std::string{};
		const auto new_remote = world_handler ? world_handler->GetLatestRemoteEndpointForRuntime(new_sid) : std::string{};
		if (!victim_remote.empty() && !new_remote.empty() && victim_remote != new_remote) {
			spdlog::info(
				"[reconnect_ip_change] account_id={} char_id={} old_sid={} old_serial={} old_remote={} new_sid={} new_serial={} new_remote={} authoritative_sid={} authoritative_serial={}",
				(victim.account_id != 0 ? victim.account_id : fallback_account_id),
				(victim.char_id != 0 ? victim.char_id : fallback_char_id),
				victim.sid,
				victim.serial,
				victim_remote,
				new_sid,
				new_serial,
				new_remote,
				new_sid,
				new_serial);
		}

		switch (stat_category) {
		case SessionKickStatCategory::DuplicateChar:
			svr::metrics::g_dup_login_char.fetch_add(1, std::memory_order_relaxed);
			break;
		case SessionKickStatCategory::DuplicateAccount:
			svr::metrics::g_dup_login_account.fetch_add(1, std::memory_order_relaxed);
			break;
		case SessionKickStatCategory::DuplicateBoth:
			svr::metrics::g_dup_login_both.fetch_add(1, std::memory_order_relaxed);
			break;
		case SessionKickStatCategory::DuplicateDeduplicatedSameSession:
			svr::metrics::g_dup_login_dedup_same_session.fetch_add(1, std::memory_order_relaxed);
			break;
		case SessionKickStatCategory::None:
		case SessionKickStatCategory::Other:
		default:
			break;
		}

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
		AbortEnterWorldFlowBySession_(
			ctx.sid,
			ctx.serial,
			"TryBeginDuplicateWorldSessionKickClose_ aborted pending enter-world flow for duplicate-login victim.");
		MarkWorldSessionCloseReason(
			ctx.sid,
			ctx.serial,
			WorldSessionCloseReason::DuplicateKick);

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
		if (!dc::IsValidSessionKey(sid, serial)) {
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

	void WorldRuntime::LogSessionCloseEvent_(
		spdlog::level::level_enum level,
		std::string_view event_text,
		const SessionCloseLogContext& ctx) const
	{
		if (ctx.trace_id != 0) {
			spdlog::log(
				level,
				"[dup_login trace={}] {} char_id={} sid={} serial={} close_reason={}",
				ctx.trace_id,
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				ToString(ctx.close_reason));
		}
		else {
			spdlog::log(
				level,
				"[session_close] {} char_id={} sid={} serial={} close_reason={}",
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				ToString(ctx.close_reason));
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
				"[dup_login trace={}] {} char_id={} sid={} serial={} removed={} close_reason={}",
				ctx.trace_id,
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				static_cast<int>(removed),
				ToString(ctx.close_reason));
		}
		else {
			spdlog::log(
				level,
				"[session_close] {} char_id={} sid={} serial={} removed={} close_reason={}",
				event_text,
				ctx.char_id,
				ctx.sid,
				ctx.serial,
				static_cast<int>(removed),
				ToString(ctx.close_reason));
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


} // namespace svr









