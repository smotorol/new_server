#include "services/world/runtime/world_runtime_private.h"
#include "services/world/db/item_template_repository.h"
#include "services/world/handler/world_handler.h"
#include "proto/common/protobuf_packet_codec.h"
#include <fmt/format.h>
#include <limits>
#include "server_common/log/enter_flow_log.h"
#include "server_common/log/flow_event_codes.h"
#include "services/world/common/string_utils.h"
#include "services/world/zone_loader/zone_content_catalog.h"

#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
#include "proto/generated/cpp/client_world.pb.h"
#define DC_WORLD_CLIENT_PROTOBUF 1
#else
#define DC_WORLD_CLIENT_PROTOBUF 0
#endif

namespace svr {

	using namespace svr::detail;

	namespace {
		proto::world::EnterWorldResultCode MapBeginEnterFailReason_(
			BeginEnterWorldSessionResultKind kind) noexcept
		{
			switch (kind) {
			case BeginEnterWorldSessionResultKind::AlreadyPending:
				return pt_w::EnterWorldResultCode::enter_already_pending;
			case BeginEnterWorldSessionResultKind::AlreadyInWorld:
				return pt_w::EnterWorldResultCode::already_in_world;
			case BeginEnterWorldSessionResultKind::Closing:
				return pt_w::EnterWorldResultCode::session_closing;
			case BeginEnterWorldSessionResultKind::InvalidInput:
			default:
				return pt_w::EnterWorldResultCode::internal_error;
			}
		}

		void SendEnterPlayerSpawn_(
			WorldHandler& handler,
			std::uint32_t dwProID,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id,
			std::int32_t x,
			std::int32_t y)
		{
#if DC_WORLD_CLIENT_PROTOBUF
			if (handler.IsSessionProtoMode(sid, serial)) {
				dc::proto::client::world::PlayerSpawn msg;
				msg.set_char_id(char_id);
				msg.set_x(x);
				msg.set_y(y);
				std::vector<char> framed;
				if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(proto::S2CMsg::player_spawn), msg, framed)) {
					_MSG_HEADER header{};
					std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
					handler.Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
					return;
				}
			}
#endif

			proto::S2C_player_spawn legacy{};
			legacy.char_id = char_id;
			legacy.x = x;
			legacy.y = y;
			const auto h = proto::make_header(static_cast<std::uint16_t>(proto::S2CMsg::player_spawn), static_cast<std::uint16_t>(sizeof(legacy)));
			handler.Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&legacy));
		}

		void SendEnterPlayerSpawnBatch_(
			WorldHandler& handler,
			std::uint32_t dwProID,
			std::uint32_t sid,
			std::uint32_t serial,
			const std::vector<proto::S2C_player_spawn_item>& spawn_items)
		{
			if (spawn_items.empty()) {
				return;
			}

#if DC_WORLD_CLIENT_PROTOBUF
			if (handler.IsSessionProtoMode(sid, serial)) {
				dc::proto::client::world::PlayerSpawnBatch msg;
				for (const auto& item : spawn_items) {
					auto* out = msg.add_items();
					out->set_char_id(item.char_id);
					out->set_x(item.x);
					out->set_y(item.y);
				}
				std::vector<char> framed;
				if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(proto::S2CMsg::player_spawn_batch), msg, framed)) {
					_MSG_HEADER header{};
					std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
					handler.Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
					return;
				}
			}
#endif

			const auto count = static_cast<std::uint16_t>(std::min<std::size_t>(spawn_items.size(), std::numeric_limits<std::uint16_t>::max()));
			const std::size_t body_size =
				sizeof(proto::S2C_player_spawn_batch) +
				(count > 0 ? (static_cast<std::size_t>(count) - 1) * sizeof(proto::S2C_player_spawn_item) : 0);
			std::vector<char> body(body_size);
			auto* pkt = reinterpret_cast<proto::S2C_player_spawn_batch*>(body.data());
			pkt->count = count;
			for (std::size_t i = 0; i < count; ++i) {
				pkt->items[i] = spawn_items[i];
			}
			auto h = proto::make_header(static_cast<std::uint16_t>(proto::S2CMsg::player_spawn_batch), static_cast<std::uint16_t>(body_size));
			handler.Send(dwProID, sid, serial, h, body.data());
		}
	}

	proto::world::EnterWorldResultCode WorldRuntime::MapZoneAssignRejectCodeToEnterWorldReason_(
		std::uint16_t zone_result_code) noexcept
	{
		switch (static_cast<pt_wz::ZoneMapAssignResultCode>(zone_result_code)) {
		case pt_wz::ZoneMapAssignResultCode::not_found:
			return pt_w::EnterWorldResultCode::zone_map_not_found;
		case pt_wz::ZoneMapAssignResultCode::capacity_full:
			return pt_w::EnterWorldResultCode::zone_map_capacity_full;
		case pt_wz::ZoneMapAssignResultCode::internal_error:
			return pt_w::EnterWorldResultCode::zone_assign_rejected;
		case pt_wz::ZoneMapAssignResultCode::ok:
		default:
			return pt_w::EnterWorldResultCode::zone_assign_rejected;
		}
	}

	proto::world::EnterWorldResultCode WorldRuntime::MapZonePlayerEnterRejectCodeToEnterWorldReason_(
		std::uint16_t zone_result_code) noexcept
	{
		switch (static_cast<pt_wz::ZonePlayerEnterResultCode>(zone_result_code)) {
		case pt_wz::ZonePlayerEnterResultCode::map_not_found:
			return pt_w::EnterWorldResultCode::zone_player_map_not_found;
		case pt_wz::ZonePlayerEnterResultCode::internal_error:
			return pt_w::EnterWorldResultCode::zone_player_enter_rejected;
		case pt_wz::ZonePlayerEnterResultCode::ok:
		default:
			return pt_w::EnterWorldResultCode::zone_player_enter_rejected;
		}
	}

	proto::world::EnterWorldResultCode WorldRuntime::MapAssignFailureToEnterWorldReason_(
		AssignMapInstanceResultKind kind,
		std::uint16_t reject_result_code) noexcept
	{
		switch (kind) {
		case AssignMapInstanceResultKind::NoZoneAvailable:
			return pt_w::EnterWorldResultCode::zone_not_available;
		case AssignMapInstanceResultKind::RequestSendFailed:
			return pt_w::EnterWorldResultCode::zone_assign_send_failed;
		case AssignMapInstanceResultKind::ResponseTimeout:
			return pt_w::EnterWorldResultCode::zone_assign_timeout;
		case AssignMapInstanceResultKind::Rejected:
			return MapZoneAssignRejectCodeToEnterWorldReason_(reject_result_code);
		case AssignMapInstanceResultKind::Pending:
		case AssignMapInstanceResultKind::Ok:
		default:
			return pt_w::EnterWorldResultCode::internal_error;
		}
	}

	void WorldRuntime::FailPendingEnterWorldConsumeRequest_(
		const PendingEnterWorldConsumeRequest& pending,
		pt_w::EnterWorldResultCode reason,
		std::string_view log_text)
	{
		CancelPendingEnterWorldSession(pending.sid, pending.serial, pending.char_id);

		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			return;
		}

		handler->SendEnterWorldResult(
			0,
			pending.sid,
			pending.serial,
			false,
			reason,
			pending.account_id,
			pending.char_id,
			pending.use_protobuf);

		spdlog::warn(
			"{} request_id={} sid={} serial={} account_id={} char_id={} token={}",
			log_text,
			pending.request_id,
			pending.sid,
			pending.serial,
			pending.account_id,
			pending.char_id,
			pending.world_token);
		dc::enterlog::LogEnterFlow(
			spdlog::level::warn,
			dc::enterlog::EnterStage::EnterFlowAborted,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			log_text);
	}



	void WorldRuntime::RollbackBoundEnterWorld_(
		const PendingEnterWorldConsumeRequest& pending,
		std::string_view reason_log,
		const LeaveWorldContext* leave_ctx)
	{
		CancelPendingEnterWorldSession(pending.sid, pending.serial, pending.char_id);

		const auto rollback = UnbindAuthenticatedWorldSessionBySid(pending.sid, pending.serial);

		ClosedAuthedSessionContext rollback_ctx{};
		rollback_ctx.unbind_kind = rollback.kind;
		rollback_ctx.account_id = rollback.session.account_id;
		rollback_ctx.char_id = rollback.session.char_id;
		rollback_ctx.sid = pending.sid;
		rollback_ctx.serial = pending.serial;

		if (leave_ctx != nullptr) {
			BeginLeaveWorld_(*leave_ctx, "RollbackBoundEnterWorld_ started leave cleanup.");
		}
		else {
			CleanupClosedWorldSessionActors_(rollback_ctx);
		}

		spdlog::warn(
			"{} sid={} serial={} account_id={} char_id={}",
			reason_log,
			pending.sid,
			pending.serial,
			pending.account_id,
			pending.char_id);
		dc::enterlog::LogEnterFlow(
			spdlog::level::warn,
			dc::enterlog::EnterStage::EnterFlowAborted,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			reason_log);
	}



	void WorldRuntime::FinalizeEnterWorldSuccess_(
		const PendingEnterWorldConsumeRequest& pending,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view login_session,
		std::string_view world_token,
		std::uint16_t assigned_zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		const CharacterCoreState& core_state)
	{
		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			RollbackBoundEnterWorld_(pending, "FinalizeEnterWorldSuccess_ rolled back because world handler is null.");
			return;
		}

		const auto request_id = next_zone_player_enter_request_id_++;
		PendingZonePlayerEnterRequest pending_enter{};
		pending_enter.trace_id = pending.trace_id;
		pending_enter.request_id = request_id;
		pending_enter.enter_pending = pending;
		pending_enter.target_zone_id = assigned_zone_id;
		pending_enter.map_template_id = map_template_id;
		pending_enter.instance_id = instance_id;
		pending_enter.core_state = core_state;
		pending_enter.cached_state_blob = svr::demo::SerializeDemo(core_state);
		pending_enter.issued_at = std::chrono::steady_clock::now();

		if (auto route = FindZoneRouteByZoneId_(assigned_zone_id); route.has_value()) {
			pending_enter.target_sid = route->sid;
			pending_enter.target_serial = route->serial;
			pending_enter.target_zone_server_id = route->zone_server_id;
		}

		pending_zone_player_enter_requests_[request_id] = pending_enter;
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldZoneAssignResult,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			{},
			"zone_route_resolved_player_enter_pending");

		if (!SendZonePlayerEnter_(pending.trace_id, assigned_zone_id, request_id, char_id, map_template_id, instance_id)) {
			pending_zone_player_enter_requests_.erase(request_id);
			RollbackBoundEnterWorld_(pending, "FinalizeEnterWorldSuccess_ rolled back because zone player enter send failed.");
			FailPendingEnterWorldConsumeRequest_(
				pending,
				pt_w::EnterWorldResultCode::zone_player_enter_send_failed,
				"FinalizeEnterWorldSuccess_ failed: zone player enter send failed.");
			return;
		}
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::ZonePlayerEnterRequestReceived,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			{},
			"world_sent_zone_player_enter");
	}

	void WorldRuntime::CompleteEnterWorldSuccessAfterZoneAck_(
		const PendingZonePlayerEnterRequest& pending_enter)
	{
		const auto& pending = pending_enter.enter_pending;

		if (!PromoteEnterWorldSessionToInWorld(
			pending.sid,
			pending.serial,
			pending.char_id)) {
			spdlog::warn(
				"CompleteEnterWorldSuccessAfterZoneAck_ stale ack ignored. request_id={} sid={} serial={} account_id={} char_id={}",
				pending_enter.request_id,
				pending.sid,
				pending.serial,
				pending.account_id,
				pending.char_id);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"stale_zone_player_enter_ack");
			return;
		}

		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			RollbackBoundEnterWorld_(pending, "CompleteEnterWorldSuccessAfterZoneAck_ rolled back because world handler is null.");
			return;
		}

		const auto char_id = pending.char_id;
		const auto assigned_zone_id = pending_enter.target_zone_id;
		const auto map_template_id = pending_enter.map_template_id;
		const auto instance_id = pending_enter.instance_id;
		const auto core_state = pending_enter.core_state;
		const Vec2i start_pos{ core_state.hot.position.x, core_state.hot.position.y };

		PostActor(char_id, [this, trace_id = pending.trace_id, char_id, sid = pending.sid, serial = pending.serial, assigned_zone_id, map_template_id, instance_id, core_state, start_pos]() {
			auto& a = GetOrCreatePlayerActor(char_id);
			ApplyCharacterCoreStateToActor_(a, core_state, sid, serial, assigned_zone_id, map_template_id, instance_id);

			PostActor(svr::MakeZoneActorId(a.GetZoneId()), [this, trace_id, char_id, sid, serial, assigned_zone_id, map_template_id, instance_id, start_pos]() {
				auto& z = GetOrCreateZoneActor(assigned_zone_id);
				z.JoinOrUpdate(char_id, start_pos, sid, serial);

				auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
				if (!handler) {
					return;
				}

				std::vector<proto::S2C_player_spawn_item> initial_spawn_items;
				std::vector<std::pair<std::uint32_t, std::uint32_t>> remote_receivers;
				if (const auto self_it = z.players.find(char_id); self_it != z.players.end()) {
					auto visible_ids = z.GatherNeighborsVec(self_it->second.cx, self_it->second.cy);
					visible_ids.erase(
						std::remove(visible_ids.begin(), visible_ids.end(), char_id),
						visible_ids.end());
					for (const auto other_char_id : visible_ids) {
						auto it = z.players.find(other_char_id);
						if (it == z.players.end()) {
							continue;
						}

						proto::S2C_player_spawn_item item{};
						item.char_id = other_char_id;
						item.x = it->second.pos.x;
						item.y = it->second.pos.y;
						initial_spawn_items.push_back(item);

						if (it->second.sid != 0 && it->second.serial != 0) {
							remote_receivers.emplace_back(it->second.sid, it->second.serial);
						}
					}
				}

				const bool zone_snapshot_requested = RequestZoneInitialSnapshot(
					trace_id,
					sid,
					serial,
					char_id,
					assigned_zone_id,
					map_template_id,
					instance_id,
					start_pos.x,
					start_pos.y,
					ZoneSnapshotReason::enter);

				if (!zone_snapshot_requested) {
					spdlog::warn(
						"enter initial snapshot fallback disabled. char_id={} sid={} serial={} zone_id={} would_have_spawn_batch_count={}",
						char_id,
						sid,
						serial,
						assigned_zone_id,
						initial_spawn_items.size());
				}

				spdlog::info(
					"enter initial aoi sync prepared. char_id={} sid={} serial={} zone_id={} spawn_batch_count={} remote_spawn_notify_count={} zone_snapshot_requested={}",
					char_id,
					sid,
					serial,
					assigned_zone_id,
					initial_spawn_items.size(),
					remote_receivers.size(),
					zone_snapshot_requested ? 1 : 0);
			});
		});

		if (!pending_enter.cached_state_blob.empty()) {
			spdlog::info("[{}] accepted request_id={} sid={} serial={} account_id={} char_id={} bind_kind={} duplicate_cause={}",
				dc::logevt::world::kTicketConsumeResp,
				pending.request_id,
				pending_enter.request_id,
				pending.sid,
				pending.serial,
				pending.account_id,
				char_id,
				"Accepted",
				"None");

			const std::uint32_t world_code = 0;
			CacheCharacterState(world_code, char_id, pending_enter.cached_state_blob);
		}
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldSessionBound,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			{},
			"zone_ack_promoted_session");

		if (!NotifyAccountWorldEnterSuccess(pending.trace_id, pending.account_id, pending.char_id, pending.login_session, pending.world_token)) {
			const LeaveWorldContext leave_ctx{
				.char_id = char_id,
				.sid = pending.sid,
				.serial = pending.serial,
				.zone_id = assigned_zone_id,
				.map_template_id = map_template_id,
				.instance_id = instance_id,
			};
			RollbackBoundEnterWorld_(
				pending,
				"CompleteEnterWorldSuccessAfterZoneAck_ rolled back because account world_enter_success_notify send failed.",
				&leave_ctx);
			FailPendingEnterWorldConsumeRequest_(
				pending,
				pt_w::EnterWorldResultCode::account_enter_notify_failed,
				"CompleteEnterWorldSuccessAfterZoneAck_ failed: account world_enter_success_notify send failed.");
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"world_notify_relay_failed");
			return;
		}
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldEnterSuccessNotifySent,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token });

		proto::S2C_actor_bound bound{};
		bound.actor_id = char_id;
		auto bh = proto::make_header(
			static_cast<std::uint16_t>(proto::S2CMsg::actor_bound),
			static_cast<std::uint16_t>(sizeof(bound)));
		handler->Send(0, pending.sid, pending.serial, bh, reinterpret_cast<const char*>(&bound));

		handler->SendEnterWorldResult(
			0,
			pending.sid,
			pending.serial,
			true,
			pt_w::EnterWorldResultCode::success,
			pending.account_id,
			pending.char_id,
			pending.use_protobuf,
			GetOrCreateReconnectTokenForSession(pending.sid, pending.serial));

		handler->SendZoneMapState(
			0,
			pending.sid,
			pending.serial,
			pending.char_id,
			assigned_zone_id,
			map_template_id,
			core_state.hot.position.x,
			core_state.hot.position.y,
			proto::ZoneMapStateReason::enter_success);

		if (const auto content = svr::zonecontent::Find(assigned_zone_id, map_template_id); content.has_value()) {
			spdlog::info(
				"enter world content ready. char_id={} zone_id={} map_id={} wm_present={} portals={} npcs={} monsters={} safe={} special={} dir={}",
				pending.char_id,
				assigned_zone_id,
				map_template_id,
				std::filesystem::exists(content->map_wm_path) ? 1 : 0,
				content->portal_count,
				content->npc_count,
				content->monster_count,
				content->safe_count,
				content->special_count,
				content->directory.string());
		}
	}

	bool WorldRuntime::RequestCharacterEnterSnapshotLoad_(
		const PendingEnterWorldConsumeRequest& pending)
	{
		const auto request_id = next_world_auth_consume_request_id_.fetch_add(1, std::memory_order_relaxed);

		svr::dqs_payload::WorldCharacterEnterSnapshot payload{};
		payload.world_id = world_id_;
		payload.sid = pending.sid;
		payload.serial = pending.serial;
		payload.trace_id = pending.trace_id;
		payload.request_id = request_id;
		payload.account_id = pending.account_id;
		payload.char_id = pending.char_id;

		if (auto blob = TryLoadCharacterState(world_id_, pending.char_id); blob.has_value()) {
			const auto copy_size = static_cast<std::uint16_t>(std::min<std::size_t>(blob->size(), dc::k_character_state_blob_max_len));
			payload.cached_state_blob_size = copy_size;
			if (copy_size > 0) {
				std::memcpy(payload.cached_state_blob, blob->data(), copy_size);
			}
		}

		pending_character_enter_snapshot_requests_[request_id] = PendingCharacterEnterSnapshotRequest{
			pending.trace_id,
			request_id,
			pending,
			std::chrono::steady_clock::now()
		};

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldCharacterSnapshotLoadRequested,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			{},
			"snapshot_load_requested");

		if (!PushDQSData(
			static_cast<std::uint8_t>(svr::dqs::ProcessCode::world),
			static_cast<std::uint8_t>(svr::dqs::QueryCase::world_character_enter_snapshot),
			reinterpret_cast<const char*>(&payload),
			static_cast<int>(sizeof(payload)))) {
			pending_character_enter_snapshot_requests_.erase(request_id);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"snapshot_load_enqueue_failed");
			return false;
		}

		return true;
	}

	void WorldRuntime::ApplyCharacterCoreStateToActor_(
		PlayerActor& actor,
		const CharacterCoreState& core_state,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint16_t assigned_zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id)
	{
		actor.core_state = core_state;
		actor.MutableCoreState().hot.in_world = true;
		actor.MutableCoreState().hot.position.zone_id = assigned_zone_id;
		actor.MutableCoreState().hot.position.map_id = map_template_id;
		actor.RecomputeCombatRuntimeStats();
		actor.bind_session(sid, serial);
		actor.map_instance_id = instance_id;
		spdlog::debug(
			"Character combat stats recomputed on enter. char_id={} level={} job={} tribe={} weapon={} armor={} accessory={} costume={} max_hp={} max_mp={} atk={} def={}",
			actor.char_id,
			actor.MutableCoreState().identity.level,
			actor.MutableCoreState().identity.job,
			actor.MutableCoreState().identity.tribe,
			actor.MutableCoreState().equip.weapon_template_id,
			actor.MutableCoreState().equip.armor_template_id,
			actor.MutableCoreState().equip.accessory_template_id,
			actor.MutableCoreState().equip.costume_template_id,
			actor.GetMaxHp(),
			actor.GetMaxMp(),
			actor.GetAttack(),
			actor.GetDefense());
	}

	void WorldRuntime::OnCharacterEnterSnapshotResult_(
		const svr::dqs_result::WorldCharacterEnterSnapshotResult& rr)
	{
		auto it = pending_character_enter_snapshot_requests_.find(rr.request_id);
		if (it == pending_character_enter_snapshot_requests_.end()) {
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ rr.trace_id, rr.account_id, rr.char_id, rr.sid, rr.serial },
				"stale_character_snapshot_result");
			return;
		}

		auto pending_snapshot = std::move(it->second);
		pending_character_enter_snapshot_requests_.erase(it);
		auto pending = pending_snapshot.enter_pending;

		if (!rr.found || rr.result != svr::dqs::ResultCode::success || !rr.core_state.identity.valid()) {
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::WorldCharacterSnapshotLoadResult,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				rr.fail_reason,
				"snapshot_load_failed");
			FailPendingEnterWorldConsumeRequest_(
				pending,
				pt_w::EnterWorldResultCode::internal_error,
				"Character enter snapshot load failed.");
			return;
		}

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldCharacterSnapshotLoadResult,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			{},
			rr.cache_blob_applied ? "snapshot_loaded_with_cache_blob" : "snapshot_loaded_db_only");

		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			return;
		}

		const auto begin_result = TryBeginEnterWorldSession(
			pending.sid,
			pending.serial,
			pending.account_id,
			pending.char_id);

		if (!begin_result.started()) {
			handler->SendEnterWorldResult(
				0,
				pending.sid,
				pending.serial,
				false,
				MapBeginEnterFailReason_(begin_result.kind),
				pending.account_id,
				pending.char_id,
				pending.use_protobuf);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				fmt::format(
					"begin_enter_world_rejected_after_snapshot kind={} stage={}",
					static_cast<int>(begin_result.kind),
					static_cast<int>(begin_result.stage)));
			return;
		}

		const auto bind_result = BindAuthenticatedWorldSessionForLogin(
			pending.account_id,
			pending.char_id,
			pending.sid,
			pending.serial,
			static_cast<std::uint16_t>(pt_w::WorldKickReason::duplicate_login));

		if (bind_result.kind == BindAuthedWorldSessionResultKind::InvalidInput) {
			CancelPendingEnterWorldSession(pending.sid, pending.serial, pending.char_id);
			handler->SendEnterWorldResult(
				0,
				pending.sid,
				pending.serial,
				false,
				pt_w::EnterWorldResultCode::bind_invalid_input,
				pending.account_id,
				pending.char_id,
				pending.use_protobuf);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"world_session_bind_failed_after_snapshot");
			return;
		}

		auto core_state = rr.core_state;
		core_state.hot.in_world = true;
		core_state.hot.dirty_flags |=
			svr::CharacterDirtyFlags::resources |
			svr::CharacterDirtyFlags::position;
		core_state.hot.version += 1;

		const auto map_template_id = core_state.hot.position.map_id != 0 ? core_state.hot.position.map_id : 1;
		const auto instance_id = 0u;
		const auto map_assignment = AssignMapInstance(map_template_id, instance_id, true, false, pending.trace_id);

		switch (map_assignment.kind) {
		case AssignMapInstanceResultKind::Ok:
			dc::enterlog::LogEnterFlow(
				spdlog::level::info,
				dc::enterlog::EnterStage::WorldZoneAssignResult,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				{},
				fmt::format(
					"zone_assign_immediate_success zone_id={} map_template_id={} instance_id={}",
					map_assignment.zone_id,
					map_template_id,
					instance_id));
			FinalizeEnterWorldSuccess_(
				pending,
				pending.account_id,
				pending.char_id,
				pending.login_session,
				pending.world_token,
				map_assignment.zone_id,
				map_template_id,
				instance_id,
				core_state);
			return;

		case AssignMapInstanceResultKind::Pending:
			{
				dc::enterlog::LogEnterFlow(
					spdlog::level::info,
					dc::enterlog::EnterStage::WorldZoneAssignRequestSent,
					{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
					{},
					fmt::format(
						"zone_assign_pending request_id={} map_template_id={} instance_id={}",
						map_assignment.request_id,
						map_template_id,
						instance_id));
				PendingEnterWorldFinalize finalize{};
				finalize.assign_request_id = map_assignment.request_id;
				finalize.enter_pending = pending;
				finalize.map_template_id = map_template_id;
				finalize.instance_id = instance_id;
				finalize.core_state = std::move(core_state);
				pending_enter_world_finalize_by_assign_request_[map_assignment.request_id].push_back(std::move(finalize));
				return;
			}

		case AssignMapInstanceResultKind::NoZoneAvailable:
		case AssignMapInstanceResultKind::RequestSendFailed:
		case AssignMapInstanceResultKind::ResponseTimeout:
		case AssignMapInstanceResultKind::Rejected:
		default:
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::WorldZoneAssignResult,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"world_zone_assign_failed",
				fmt::format(
					"kind={} reject_result_code={} map_template_id={} instance_id={}",
					static_cast<int>(map_assignment.kind),
					map_assignment.reject_result_code,
					map_template_id,
					instance_id));
			RollbackBoundEnterWorld_(
				pending,
				"OnCharacterEnterSnapshotResult_ rolled back because map assignment failed.");

			FailPendingEnterWorldConsumeRequest_(
				pending,
				MapAssignFailureToEnterWorldReason_(map_assignment.kind, map_assignment.reject_result_code),
				"OnCharacterEnterSnapshotResult_ map assignment failed.");
			return;
		}
	}



bool WorldRuntime::RequestConsumeWorldAuthTicket(
		std::uint32_t sid,
		std::uint32_t serial,
		bool use_protobuf,
		std::uint64_t trace_id,
		std::uint64_t account_id,
		std::string_view login_session,
		std::string_view token)
	{
		if (!account_ready_.load(std::memory_order_acquire)) {
			spdlog::warn("[{}] skipped: account line not ready. sid={} serial={} account_id={}",
				dc::logevt::world::kTicketConsumeReq,
				sid, serial, account_id);
			return false;
		}

		if (!world_account_handler_) {
			spdlog::warn("[{}] skipped: world_account_handler_ is null. sid={} serial={} account_id={}",
				dc::logevt::world::kTicketConsumeReq,
				sid, serial, account_id);
			return false;
		}
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldTokenConsumeRequested,
			{ trace_id, account_id, 0, sid, serial, login_session, token },
			{},
			"world_consume_started");

		const auto request_id = next_world_auth_consume_request_id_.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			pending_enter_world_consume_[request_id] = PendingEnterWorldConsumeRequest{
				trace_id,
				request_id,
				sid,
				serial,
				use_protobuf,
				account_id,
				0,
				std::string(login_session),
				std::string(token),
				std::chrono::steady_clock::now()
			};
		}

		const bool sent = world_account_handler_->SendWorldAuthTicketConsumeRequest(
			0,
			account_sid_.load(std::memory_order_relaxed),
			account_serial_.load(std::memory_order_relaxed),
			trace_id,
			request_id,
			account_id,
			login_session,
			token);

		if (!sent) {
			std::lock_guard lk(pending_enter_world_consume_mtx_);
			pending_enter_world_consume_.erase(request_id);
			spdlog::warn("[{}] send failed request_id={} sid={} serial={} account_id={}",
				dc::logevt::world::kTicketConsumeReq, request_id, sid, serial, account_id);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ trace_id, account_id, 0, sid, serial, login_session, token },
				"world_token_consume_send_failed");
		} else {
			spdlog::info("[{}] sent request_id={} sid={} serial={} account_id={} token={}",
				dc::logevt::world::kTicketConsumeReq, request_id, sid, serial, account_id, token);
		}

		return sent;
	}



	void WorldRuntime::OnWorldAuthTicketConsumeResponse(
		std::uint64_t trace_id,
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
				spdlog::warn("[{}] stale response ignored. request_id={} account_id={} char_id={}",
					dc::logevt::world::kTicketConsumeResp,
					request_id,
					account_id,
					char_id);
				dc::enterlog::LogEnterFlow(
					spdlog::level::warn,
					dc::enterlog::EnterStage::EnterFlowAborted,
					{ trace_id, account_id, char_id, 0, 0, login_session, world_token },
					"stale_world_token_consume_response");
				return;
			}
			pending = std::move(it->second);
			pending_enter_world_consume_.erase(it);
		}
		if (pending.trace_id == 0) {
			pending.trace_id = trace_id;
		}
		if (pending.char_id == 0) {
			pending.char_id = char_id;
		}

		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			return;
		}

		if (pending.account_id != account_id ||
			pending.login_session != login_session ||
			pending.world_token != world_token)
		{
			spdlog::warn(
				"[{}] mismatch. request_id={} pending_account_id={} resp_account_id={} pending_login_session={} resp_login_session={} pending_token={} resp_token={}",
				dc::logevt::world::kTicketConsumeResp,
				request_id,
				pending.account_id,
				account_id,
				pending.login_session,
				login_session,
				pending.world_token,
				world_token);

			handler->SendEnterWorldResult(
				0,
				pending.sid,
				pending.serial,
				false,
				pt_w::EnterWorldResultCode::auth_ticket_mismatch,
				pending.account_id,
				pending.char_id,
				pending.use_protobuf);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::WorldTokenConsumeResult,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"consume_response_mismatch");
			return;
		}

		const std::string final_login_session =
			login_session.empty() ? pending.login_session : std::string(login_session);
		const std::string final_world_token =
			world_token.empty() ? pending.world_token : std::string(world_token);

		if (result_kind != ConsumePendingWorldAuthTicketResultKind::Ok) {
			auto reason = pt_w::EnterWorldResultCode::auth_ticket_not_found;
			switch (result_kind) {
			case ConsumePendingWorldAuthTicketResultKind::Expired:
				reason = pt_w::EnterWorldResultCode::auth_ticket_expired;
				break;
			case ConsumePendingWorldAuthTicketResultKind::ReplayDetected:
				reason = pt_w::EnterWorldResultCode::auth_ticket_replayed;
				break;
			case ConsumePendingWorldAuthTicketResultKind::AccountMismatch:
			case ConsumePendingWorldAuthTicketResultKind::CharMismatch:
			case ConsumePendingWorldAuthTicketResultKind::LoginSessionMismatch:
				reason = pt_w::EnterWorldResultCode::auth_ticket_mismatch;
				break;
			case ConsumePendingWorldAuthTicketResultKind::TokenNotFound:
			default:
				reason = pt_w::EnterWorldResultCode::auth_ticket_not_found;
				break;
			}

			handler->SendEnterWorldResult(
				0,
				pending.sid,
				pending.serial,
				false,
				reason,
				pending.account_id,
				pending.char_id,
				pending.use_protobuf);

			spdlog::warn(
				"[{}] sid={} serial={} request_id={} pending_account_id={} pending_char_id={} resp_account_id={} resp_char_id={} result_kind={}",
				dc::logevt::world::kTicketConsumeDenied,
				pending.sid,
				pending.serial,
				request_id,
				pending.account_id,
				pending.char_id,
				account_id,
				char_id,
				static_cast<int>(result_kind));
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::WorldTokenConsumeResult,
				{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
				"world_token_consume_failed");
			return;
		}
		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::WorldTokenConsumeResult,
			{ pending.trace_id, pending.account_id, pending.char_id, pending.sid, pending.serial, pending.login_session, pending.world_token },
			{},
			"world_token_consume_succeeded");
		pending.login_session = final_login_session;
		pending.world_token = final_world_token;

		if (!RequestCharacterEnterSnapshotLoad_(pending)) {
			handler->SendEnterWorldResult(
				0,
				pending.sid,
				pending.serial,
				false,
				pt_w::EnterWorldResultCode::internal_error,
				pending.account_id,
				pending.char_id,
				pending.use_protobuf);
		}
	}

} // namespace svr



