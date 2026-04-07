#include "services/world/runtime/world_runtime_private.h"

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include "server_common/config/server_topology.h"
#include "server_common/log/enter_flow_log.h"
#include "server_common/session/session_key.h"
#include "services/world/handler/world_handler.h"
#include "services/world/common/aoi_broadcast_sanitize.h"

namespace svr {

	namespace {
		static bool IsZoneAssignOk_(std::uint16_t result_code) noexcept
		{
			return result_code == static_cast<std::uint16_t>(pt_wz::ZoneMapAssignResultCode::ok);
		}

		static bool IsZonePlayerEnterOk_(std::uint16_t result_code) noexcept
		{
			return result_code == static_cast<std::uint16_t>(pt_wz::ZonePlayerEnterResultCode::ok);
		}

		static ZoneRouteInfo MakeZoneRouteInfo_(const ZoneRouteState& route) noexcept
		{
			ZoneRouteInfo current{};
			current.sid = route.sid;
			current.serial = route.serial;
			current.zone_server_id = route.server_id;
			current.zone_id = route.zone_id;
			current.channel_id = route.channel_id;
			current.active_map_instance_count = route.active_map_instance_count;
			current.active_player_count = route.active_player_count;
			current.load_score = route.load_score;
			current.flags = route.flags;
			current.served_map_ids = route.served_map_ids;
			current.last_heartbeat_at = route.last_heartbeat_at;
			return current;
		}

	static bool RouteServesMap_(const ZoneRouteState& route, std::uint32_t map_template_id) noexcept
	{
			if (route.served_map_ids.empty()) {
				return true;
			}

			return std::find(route.served_map_ids.begin(), route.served_map_ids.end(), map_template_id) != route.served_map_ids.end();
		}

		static void SendPortalSpawnPacket_(
			WorldHandler& handler,
			std::uint32_t dwProID,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id,
			std::int32_t x,
			std::int32_t y)
		{
#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
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
			auto header = proto::make_header(static_cast<std::uint16_t>(proto::S2CMsg::player_spawn), static_cast<std::uint16_t>(sizeof(legacy)));
			handler.Send(dwProID, sid, serial, header, reinterpret_cast<const char*>(&legacy));
		}

		static void SendPortalDespawnPacket_(
			WorldHandler& handler,
			std::uint32_t dwProID,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t char_id)
		{
#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
			if (handler.IsSessionProtoMode(sid, serial)) {
				dc::proto::client::world::PlayerDespawn msg;
				msg.set_char_id(char_id);
				std::vector<char> framed;
				if (dc::proto::BuildFramedMessage(static_cast<std::uint16_t>(proto::S2CMsg::player_despawn), msg, framed)) {
					_MSG_HEADER header{};
					std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
					handler.Send(dwProID, sid, serial, header, framed.data() + MSG_HEADER_SIZE);
					return;
				}
			}
#endif
			proto::S2C_player_despawn legacy{};
			legacy.char_id = char_id;
			auto header = proto::make_header(static_cast<std::uint16_t>(proto::S2CMsg::player_despawn), static_cast<std::uint16_t>(sizeof(legacy)));
			handler.Send(dwProID, sid, serial, header, reinterpret_cast<const char*>(&legacy));
		}

		static void SendPortalSpawnBatchPacket_(
			WorldHandler& handler,
			std::uint32_t dwProID,
			std::uint32_t sid,
			std::uint32_t serial,
			const std::vector<proto::S2C_player_spawn_item>& items)
		{
#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_world.pb.h")
			if (handler.IsSessionProtoMode(sid, serial)) {
				dc::proto::client::world::PlayerSpawnBatch msg;
				for (const auto& item : items) {
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
			const auto count = svr::aoi::ClampBatchEntityCount(items.size());
			const auto body_size = svr::aoi::SpawnBatchBodySize(count);
			std::vector<char> body(body_size);
			auto* pkt = reinterpret_cast<proto::S2C_player_spawn_batch*>(body.data());
			pkt->count = count;
			for (std::size_t i = 0; i < count; ++i) {
				pkt->items[i] = items[i];
			}
			auto header = proto::make_header(static_cast<std::uint16_t>(proto::S2CMsg::player_spawn_batch), static_cast<std::uint16_t>(body_size));
			handler.Send(dwProID, sid, serial, header, body.data());
		}

		static std::optional<MapAssignmentEntry> TryGetRouteAssignment_(
			const std::unordered_map<std::uint32_t, ZoneRouteState>& routes,
			std::uint32_t sid,
			std::uint32_t serial)
		{
			const auto it = routes.find(sid);
			if (it == routes.end() || it->second.serial != serial || !it->second.registered) {
				return std::nullopt;
			}

			return MapAssignmentEntry{
				it->second.server_id,
				it->second.zone_id,
				it->second.channel_id,
				0,
				0
			};
		}
	}

	std::uint64_t WorldRuntime::MakeMapAssignmentKey_(std::uint32_t map_template_id, std::uint32_t instance_id) const noexcept
	{
		return (static_cast<std::uint64_t>(map_template_id) << 32) | static_cast<std::uint64_t>(instance_id);
	}

	bool WorldRuntime::BeginPortalTransfer(
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint16_t source_zone_id,
		std::uint32_t source_map_template_id,
		std::uint32_t source_instance_id,
		std::int32_t source_x,
		std::int32_t source_y,
		std::uint32_t target_map_template_id,
		std::uint32_t target_instance_id,
		std::int32_t target_x,
		std::int32_t target_y,
		std::uint64_t trace_id)
	{
		const auto resolved_target_instance_id = ResolvePortalTargetInstanceId_(target_map_template_id, target_instance_id);

		auto assign = AssignMapInstance(
			target_map_template_id,
			resolved_target_instance_id,
			true,
			IsDungeonMapTemplate_(target_map_template_id),
			trace_id);

		if (assign.kind == AssignMapInstanceResultKind::Pending) {
			PendingPortalTransfer pending{};
			pending.trace_id = trace_id;
			pending.assign_request_id = assign.request_id;
			pending.sid = sid;
			pending.serial = serial;
			pending.char_id = char_id;
			pending.source_zone_id = source_zone_id;
			pending.source_map_template_id = source_map_template_id;
			pending.source_instance_id = source_instance_id;
			pending.source_x = source_x;
			pending.source_y = source_y;
			pending.target_map_template_id = target_map_template_id;
			pending.target_instance_id = resolved_target_instance_id;
			pending.target_x = target_x;
			pending.target_y = target_y;
			pending.issued_at = std::chrono::steady_clock::now();
			pending_portal_transfer_by_assign_request_[assign.request_id] = pending;
			spdlog::info(
				"portal transfer pending. sid={} serial={} char_id={} from_zone={} from_map={} to_map={} target_pos=({}, {}) assign_request_id={}",
				sid,
				serial,
				char_id,
				source_zone_id,
				source_map_template_id,
				target_map_template_id,
				target_x,
				target_y,
				assign.request_id);
			return true;
		}

		if (assign.kind != AssignMapInstanceResultKind::Ok) {
			spdlog::warn(
				"portal transfer route selection failed. sid={} serial={} char_id={} from_zone={} from_map={} to_map={} result_kind={}",
				sid,
				serial,
				char_id,
				source_zone_id,
				source_map_template_id,
				target_map_template_id,
				static_cast<std::uint32_t>(assign.kind));
			return false;
		}

		const auto key = MakeMapAssignmentKey_(target_map_template_id, resolved_target_instance_id);
		const auto it = map_assignments_.find(key);
		if (it == map_assignments_.end()) {
			spdlog::warn(
				"portal transfer route missing after immediate assignment. sid={} serial={} char_id={} to_map={} instance_id={}",
				sid,
				serial,
				char_id,
				target_map_template_id,
				resolved_target_instance_id);
			return false;
		}

		PendingPortalTransfer pending{};
		pending.trace_id = trace_id;
		pending.sid = sid;
		pending.serial = serial;
		pending.char_id = char_id;
		pending.source_zone_id = source_zone_id;
		pending.source_map_template_id = source_map_template_id;
		pending.source_instance_id = source_instance_id;
		pending.source_x = source_x;
		pending.source_y = source_y;
		pending.target_map_template_id = target_map_template_id;
		pending.target_instance_id = resolved_target_instance_id;
		pending.target_x = target_x;
		pending.target_y = target_y;
		CompletePortalTransfer_(
			pending,
			it->second.zone_id,
			it->second.channel_id,
			it->second.zone_server_id,
			target_map_template_id,
			resolved_target_instance_id);
		return true;
	}

	void WorldRuntime::LoadServerTopology_()
	{
		server_topology_ = dc::cfg::LoadServerTopology();
		if (!server_topology_.has_value()) {
			spdlog::warn("WorldRuntime server topology not found. map/channel route selection will use legacy zone-only fallback.");
			return;
		}

		spdlog::info(
			"WorldRuntime server topology loaded. source={} zone_processes={}",
			server_topology_->source_path.string(),
			server_topology_->zone_processes.size());
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

		PendingZoneAssignRequest pending_assign = it->second;
		if (pending_assign.target_sid != sid || pending_assign.target_serial != serial) {
			return;
		}

		pending_zone_assign_requests_.erase(it);
		pending_zone_assign_request_id_by_map_key_.erase(pending_assign.map_key);

		auto finalize_it = pending_enter_world_finalize_by_assign_request_.find(res.request_id);
		std::vector<PendingEnterWorldFinalize> finalize_list;
		if (finalize_it != pending_enter_world_finalize_by_assign_request_.end()) {
			finalize_list = std::move(finalize_it->second);
			pending_enter_world_finalize_by_assign_request_.erase(finalize_it);
		}
		auto portal_it = pending_portal_transfer_by_assign_request_.find(res.request_id);
		std::optional<PendingPortalTransfer> pending_portal;
		if (portal_it != pending_portal_transfer_by_assign_request_.end()) {
			pending_portal = portal_it->second;
			pending_portal_transfer_by_assign_request_.erase(portal_it);
		}

		if (pending_assign.map_template_id != res.map_template_id ||
			pending_assign.instance_id != res.instance_id) {
			spdlog::warn(
				"OnZoneMapAssignResponse mismatch. request_id={} sid={} serial={} req_map_template_id={} req_instance_id={} res_map_template_id={} res_instance_id={}",
				res.request_id,
				sid,
				serial,
				pending_assign.map_template_id,
				pending_assign.instance_id,
				res.map_template_id,
				res.instance_id);

			for (auto& finalize : finalize_list) {
				dc::enterlog::LogEnterFlow(
					spdlog::level::warn,
					dc::enterlog::EnterStage::WorldZoneAssignResult,
					{ finalize.enter_pending.trace_id, finalize.enter_pending.account_id, finalize.enter_pending.char_id, finalize.enter_pending.sid, finalize.enter_pending.serial, finalize.enter_pending.login_session, finalize.enter_pending.world_token },
					"world_zone_assign_response_mismatch",
					fmt::format(
						"request_id={} route_sid={} route_serial={} req_map_template_id={} req_instance_id={} res_map_template_id={} res_instance_id={}",
						res.request_id,
						sid,
						serial,
						pending_assign.map_template_id,
						pending_assign.instance_id,
						res.map_template_id,
						res.instance_id));
				RollbackBoundEnterWorld_(
					finalize.enter_pending,
					"OnZoneMapAssignResponse rolled back because response map key mismatched.");

				FailPendingEnterWorldConsumeRequest_(
					finalize.enter_pending,
					pt_w::EnterWorldResultCode::zone_assign_route_lost,
					"OnZoneMapAssignResponse map key mismatch.");
			}
			if (pending_portal.has_value()) {
				FailPendingPortalTransfer_(*pending_portal, "zone_assign_response_map_key_mismatch");
			}
			return;
		}

		if (!IsZoneAssignOk_(res.result_code)) {
			const auto reject_reason = MapZoneAssignRejectCodeToEnterWorldReason_(res.result_code);
			for (auto& finalize : finalize_list) {
				if (!IsEnterWorldSessionPending(
					finalize.enter_pending.sid,
					finalize.enter_pending.serial,
					finalize.enter_pending.char_id)) {
					dc::enterlog::LogEnterFlow(
						spdlog::level::warn,
						dc::enterlog::EnterStage::EnterFlowAborted,
						{ finalize.enter_pending.trace_id, finalize.enter_pending.account_id, finalize.enter_pending.char_id, finalize.enter_pending.sid, finalize.enter_pending.serial, finalize.enter_pending.login_session, finalize.enter_pending.world_token },
						"world_pending_enter_mismatch_after_zone_assign_reject",
						fmt::format("request_id={} zone_sid={} zone_serial={}", res.request_id, sid, serial));
					spdlog::warn(
						"OnZoneMapAssignResponse stale pending finalize dropped after reject. request_id={} world_sid={} world_serial={} char_id={}",
						res.request_id,
						finalize.enter_pending.sid,
						finalize.enter_pending.serial,
						finalize.enter_pending.char_id);
					continue;
				}

				dc::enterlog::LogEnterFlow(
					spdlog::level::warn,
					dc::enterlog::EnterStage::WorldZoneAssignResult,
					{ finalize.enter_pending.trace_id, finalize.enter_pending.account_id, finalize.enter_pending.char_id, finalize.enter_pending.sid, finalize.enter_pending.serial, finalize.enter_pending.login_session, finalize.enter_pending.world_token },
					"world_zone_assign_failed",
					fmt::format(
						"request_id={} zone_sid={} zone_serial={} result_code={} map_template_id={} instance_id={}",
						res.request_id,
						sid,
						serial,
						res.result_code,
						res.map_template_id,
						res.instance_id));
				RollbackBoundEnterWorld_(
					finalize.enter_pending,
					"OnZoneMapAssignResponse rolled back because zone rejected map assign.");

				FailPendingEnterWorldConsumeRequest_(
					finalize.enter_pending,
					reject_reason,
					"OnZoneMapAssignResponse rejected.");
			}
			if (pending_portal.has_value()) {
				FailPendingPortalTransfer_(*pending_portal, fmt::format("zone_assign_rejected result_code={}", res.result_code));
			}
			return;
		}

		map_assignments_[MakeMapAssignmentKey_(res.map_template_id, res.instance_id)] =
			MapAssignmentEntry{
				pending_assign.target_zone_server_id,
				res.zone_id,
				pending_assign.target_channel_id,
				res.map_template_id,
				res.instance_id
		};

		for (auto& finalize : finalize_list) {
			if (!IsEnterWorldSessionPending(
				finalize.enter_pending.sid,
				finalize.enter_pending.serial,
				finalize.enter_pending.char_id)) {
				dc::enterlog::LogEnterFlow(
					spdlog::level::warn,
					dc::enterlog::EnterStage::EnterFlowAborted,
					{ finalize.enter_pending.trace_id, finalize.enter_pending.account_id, finalize.enter_pending.char_id, finalize.enter_pending.sid, finalize.enter_pending.serial, finalize.enter_pending.login_session, finalize.enter_pending.world_token },
					"world_pending_enter_mismatch_after_zone_assign_success",
					fmt::format("request_id={} zone_sid={} zone_serial={}", res.request_id, sid, serial));
				spdlog::warn(
					"OnZoneMapAssignResponse stale pending finalize dropped after success. request_id={} world_sid={} world_serial={} char_id={}",
					res.request_id,
					finalize.enter_pending.sid,
					finalize.enter_pending.serial,
					finalize.enter_pending.char_id);
				continue;
			}

			dc::enterlog::LogEnterFlow(
				spdlog::level::info,
				dc::enterlog::EnterStage::WorldZoneAssignResult,
				{ finalize.enter_pending.trace_id, finalize.enter_pending.account_id, finalize.enter_pending.char_id, finalize.enter_pending.sid, finalize.enter_pending.serial, finalize.enter_pending.login_session, finalize.enter_pending.world_token },
				{},
				fmt::format(
					"zone_assign_succeeded request_id={} zone_sid={} zone_serial={} zone_id={} map_template_id={} instance_id={}",
					res.request_id,
					sid,
					serial,
					res.zone_id,
					res.map_template_id,
					res.instance_id));
			FinalizeEnterWorldSuccess_(
				finalize.enter_pending,
				finalize.enter_pending.account_id,
				finalize.enter_pending.char_id,
				finalize.enter_pending.login_session,
				finalize.enter_pending.world_token,
				res.zone_id,
				finalize.map_template_id,
				finalize.instance_id,
				finalize.core_state);
		}

		if (pending_portal.has_value()) {
			CompletePortalTransfer_(
				*pending_portal,
				res.zone_id,
				pending_assign.target_channel_id,
				pending_assign.target_zone_server_id,
				res.map_template_id,
				res.instance_id);
		}
	}

	void WorldRuntime::OnZonePlayerEnterAck(std::uint32_t sid, std::uint32_t serial, const pt_wz::ZoneWorldPlayerEnterAck& ack)
	{
		auto it = pending_zone_player_enter_requests_.find(ack.request_id);
		if (it == pending_zone_player_enter_requests_.end()) {
			return;
		}

		PendingZonePlayerEnterRequest pending_enter = it->second;
		if (pending_enter.target_sid != sid || pending_enter.target_serial != serial) {
			return;
		}

		if (!IsEnterWorldSessionPending(
			pending_enter.enter_pending.sid,
			pending_enter.enter_pending.serial,
			pending_enter.enter_pending.char_id)) {
			pending_zone_player_enter_requests_.erase(it);
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::EnterFlowAborted,
				{ pending_enter.enter_pending.trace_id, pending_enter.enter_pending.account_id, pending_enter.enter_pending.char_id, pending_enter.enter_pending.sid, pending_enter.enter_pending.serial, pending_enter.enter_pending.login_session, pending_enter.enter_pending.world_token },
				"world_pending_enter_mismatch_before_zone_player_enter_ack",
				fmt::format("request_id={} zone_sid={} zone_serial={}", ack.request_id, sid, serial));
			spdlog::warn(
				"OnZonePlayerEnterAck stale enter pending dropped. request_id={} sid={} serial={} world_sid={} world_serial={} char_id={}",
				ack.request_id,
				sid,
				serial,
				pending_enter.enter_pending.sid,
				pending_enter.enter_pending.serial,
				pending_enter.enter_pending.char_id);
			return;
		}

		pending_zone_player_enter_requests_.erase(it);

		if (pending_enter.enter_pending.char_id != ack.char_id ||
			pending_enter.map_template_id != ack.map_template_id ||
			pending_enter.instance_id != ack.instance_id ||
			pending_enter.target_zone_id != ack.zone_id) {
			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::ZonePlayerEnterResult,
				{ pending_enter.enter_pending.trace_id, pending_enter.enter_pending.account_id, pending_enter.enter_pending.char_id, pending_enter.enter_pending.sid, pending_enter.enter_pending.serial, pending_enter.enter_pending.login_session, pending_enter.enter_pending.world_token },
				"zone_player_enter_ack_mismatch",
				fmt::format(
					"request_id={} zone_sid={} zone_serial={} pending_zone_id={} ack_zone_id={} pending_map_template_id={} ack_map_template_id={} pending_instance_id={} ack_instance_id={}",
					ack.request_id,
					sid,
					serial,
					pending_enter.target_zone_id,
					ack.zone_id,
					pending_enter.map_template_id,
					ack.map_template_id,
					pending_enter.instance_id,
					ack.instance_id));
			spdlog::warn(
				"OnZonePlayerEnterAck mismatch. request_id={} sid={} serial={} pending_char_id={} ack_char_id={} pending_zone_id={} ack_zone_id={} pending_map_template_id={} ack_map_template_id={} pending_instance_id={} ack_instance_id={}",
				ack.request_id,
				sid,
				serial,
				pending_enter.enter_pending.char_id,
				ack.char_id,
				pending_enter.target_zone_id,
				ack.zone_id,
				pending_enter.map_template_id,
				ack.map_template_id,
				pending_enter.instance_id,
				ack.instance_id);

			RollbackBoundEnterWorld_(
				pending_enter.enter_pending,
				"OnZonePlayerEnterAck rolled back because response payload mismatched.");
			FailPendingEnterWorldConsumeRequest_(
				pending_enter.enter_pending,
				pt_w::EnterWorldResultCode::internal_error,
				"OnZonePlayerEnterAck mismatch.");
			return;
		}

		if (!IsZonePlayerEnterOk_(ack.result_code)) {
			if (!IsEnterWorldSessionPending(
				pending_enter.enter_pending.sid,
				pending_enter.enter_pending.serial,
				pending_enter.enter_pending.char_id)) {
				dc::enterlog::LogEnterFlow(
					spdlog::level::warn,
					dc::enterlog::EnterStage::EnterFlowAborted,
					{ pending_enter.enter_pending.trace_id, pending_enter.enter_pending.account_id, pending_enter.enter_pending.char_id, pending_enter.enter_pending.sid, pending_enter.enter_pending.serial, pending_enter.enter_pending.login_session, pending_enter.enter_pending.world_token },
					"world_pending_enter_mismatch_after_zone_player_reject",
					fmt::format("request_id={} zone_sid={} zone_serial={}", ack.request_id, sid, serial));
				spdlog::warn(
					"OnZonePlayerEnterAck stale reject dropped. request_id={} world_sid={} world_serial={} char_id={}",
					ack.request_id,
					pending_enter.enter_pending.sid,
					pending_enter.enter_pending.serial,
					pending_enter.enter_pending.char_id);
				return;
			}

			dc::enterlog::LogEnterFlow(
				spdlog::level::warn,
				dc::enterlog::EnterStage::ZonePlayerEnterResult,
				{ pending_enter.enter_pending.trace_id, pending_enter.enter_pending.account_id, pending_enter.enter_pending.char_id, pending_enter.enter_pending.sid, pending_enter.enter_pending.serial, pending_enter.enter_pending.login_session, pending_enter.enter_pending.world_token },
				"zone_player_enter_failed",
				fmt::format(
					"request_id={} zone_sid={} zone_serial={} result_code={} zone_id={} map_template_id={} instance_id={}",
					ack.request_id,
					sid,
					serial,
					ack.result_code,
					ack.zone_id,
					ack.map_template_id,
					ack.instance_id));
			RollbackBoundEnterWorld_(
				pending_enter.enter_pending,
				"OnZonePlayerEnterAck rolled back because zone rejected player enter.");
			FailPendingEnterWorldConsumeRequest_(
				pending_enter.enter_pending,
				MapZonePlayerEnterRejectCodeToEnterWorldReason_(ack.result_code),
				"OnZonePlayerEnterAck rejected.");
			return;
		}

		dc::enterlog::LogEnterFlow(
			spdlog::level::info,
			dc::enterlog::EnterStage::ZonePlayerEnterResult,
			{ pending_enter.enter_pending.trace_id, pending_enter.enter_pending.account_id, pending_enter.enter_pending.char_id, pending_enter.enter_pending.sid, pending_enter.enter_pending.serial, pending_enter.enter_pending.login_session, pending_enter.enter_pending.world_token },
			{},
			fmt::format(
				"zone_player_enter_succeeded request_id={} zone_sid={} zone_serial={} zone_id={} map_template_id={} instance_id={}",
				ack.request_id,
				sid,
				serial,
				ack.zone_id,
				ack.map_template_id,
				ack.instance_id));
		CompleteEnterWorldSuccessAfterZoneAck_(pending_enter);
	}

	void WorldRuntime::OnZoneAoiSnapshotFromHandler(
		std::uint32_t sid,
		std::uint32_t serial,
		const pt_wz::ZoneWorldAoiSnapshot& snapshot,
		const proto::S2C_player_spawn_item* items,
		std::size_t count)
	{
		auto* handler = lines_.host(WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			spdlog::warn("OnZoneAoiSnapshotFromHandler handler missing. zone_sid={} zone_serial={} char_id={}", sid, serial, snapshot.char_id);
			return;
		}

		{
			std::lock_guard lk(pending_zone_aoi_snapshot_mtx_);
			pending_zone_aoi_snapshot_by_session_key_.erase(dc::PackSessionKey(snapshot.sid, snapshot.serial));
		}

		std::uint32_t zone_server_id = 0;
		{
			std::lock_guard lk(service_line_mtx_);
			if (const auto route = TryGetRouteAssignment_(zone_routes_by_sid_, sid, serial); route.has_value()) {
				zone_server_id = route->zone_server_id;
			}
		}

		handler->SendZoneMapState(
			0,
			snapshot.sid,
			snapshot.serial,
			snapshot.char_id,
			snapshot.zone_id,
			snapshot.map_template_id,
			snapshot.self_x,
			snapshot.self_y,
			proto::ZoneMapStateReason::position_update,
			snapshot.channel_id,
			zone_server_id);

		if (count > 0 && items != nullptr) {
			std::vector<proto::S2C_player_spawn_item> spawn_items(items, items + count);
			handler->SendPlayerSpawnBatchRelay(0, snapshot.sid, snapshot.serial, spawn_items);
		}

		spdlog::info("WorldRuntime relayed zone AOI snapshot. zone_sid={} zone_serial={} client_sid={} client_serial={} char_id={} map={} channel={} instance={} zone_server_id={} count={}",
			sid, serial, snapshot.sid, snapshot.serial, snapshot.char_id, snapshot.map_template_id, snapshot.channel_id, snapshot.instance_id, zone_server_id, count);
	}

	bool WorldRuntime::RequestZoneInitialSnapshot(
		std::uint64_t trace_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint16_t zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::int32_t x,
		std::int32_t y,
		ZoneSnapshotReason reason)
	{
		if (!enable_zone_aoi_snapshot_) {
			return false;
		}
		const auto route = ResolveZoneRouteForMapInstance_(zone_id, map_template_id, instance_id);
		if (!route.has_value()) {
			spdlog::warn(
				"RequestZoneInitialSnapshot route missing. sid={} serial={} char_id={} zone_id={} map={} instance={} reason={}",
				sid,
				serial,
				char_id,
				zone_id,
				map_template_id,
				instance_id,
				static_cast<int>(reason));
			return false;
		}

		auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>();
		if (!handler) {
			return false;
		}

		if (!handler->SendPlayerMoveInternal(
			0,
			route->sid,
			route->serial,
			trace_id,
			static_cast<std::uint64_t>(reason),
			char_id,
			sid,
			serial,
			map_template_id,
			instance_id,
			zone_id,
			route->channel_id,
			x,
			y)) {
			return false;
		}

		PendingZoneAoiSnapshotRequest pending{};
		pending.trace_id = trace_id;
		pending.sid = sid;
		pending.serial = serial;
		pending.char_id = char_id;
		pending.zone_id = zone_id;
		pending.channel_id = route->channel_id;
		pending.zone_server_id = route->zone_server_id;
		pending.map_template_id = map_template_id;
		pending.instance_id = instance_id;
		pending.x = x;
		pending.y = y;
		pending.reason = reason;
		pending.issued_at = std::chrono::steady_clock::now();
		{
			std::lock_guard lk(pending_zone_aoi_snapshot_mtx_);
			pending_zone_aoi_snapshot_by_session_key_[dc::PackSessionKey(sid, serial)] = pending;
		}

		spdlog::info(
			"Zone initial snapshot requested. sid={} serial={} char_id={} zone_server={} zone_id={} map={} channel={} instance={} pos=({}, {}) reason={}",
			sid,
			serial,
			char_id,
			route->zone_server_id,
			zone_id,
			map_template_id,
			route->channel_id,
			instance_id,
			x,
			y,
			static_cast<int>(reason));
		return true;
	}

	bool WorldRuntime::MirrorPlayerMoveToZone(
		std::uint64_t trace_id,
		std::uint64_t request_id,
		std::uint32_t sid,
		std::uint32_t serial,
		std::uint64_t char_id,
		std::uint16_t zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id,
		std::int32_t x,
		std::int32_t y)
	{
		if (!enable_zone_aoi_snapshot_ && !enable_zone_aoi_move_diff_) {
			return false;
		}
		const auto route = ResolveZoneRouteForMapInstance_(zone_id, map_template_id, instance_id);
		if (!route.has_value()) {
			return false;
		}
		auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>();
		if (!handler) {
			return false;
		}
		return handler->SendPlayerMoveInternal(
			0,
			route->sid,
			route->serial,
			trace_id,
			request_id,
			char_id,
			sid,
			serial,
			map_template_id,
			instance_id,
			zone_id,
			route->channel_id,
			x,
			y);
	}

	void WorldRuntime::OnZoneAoiSpawnBatchFromHandler(
		std::uint32_t sid,
		std::uint32_t serial,
		const pt_wz::ZoneWorldAoiSpawnBatch& batch,
		const proto::S2C_player_spawn_item* items,
		std::size_t count)
	{
		auto* handler = lines_.host(WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			spdlog::warn("OnZoneAoiSpawnBatchFromHandler handler missing. zone_sid={} zone_serial={} char_id={}", sid, serial, batch.char_id);
			return;
		}
		if (count > 0 && items != nullptr) {
			std::vector<proto::S2C_player_spawn_item> spawn_items(items, items + count);
			handler->SendPlayerSpawnBatchRelay(0, batch.sid, batch.serial, spawn_items);
		}
		spdlog::info("WorldRuntime relayed zone AOI spawn batch. zone_sid={} zone_serial={} client_sid={} client_serial={} char_id={} map={} channel={} instance={} count={}",
			sid, serial, batch.sid, batch.serial, batch.char_id, batch.map_template_id, batch.channel_id, batch.instance_id, count);
	}

	void WorldRuntime::OnZoneAoiDespawnBatchFromHandler(
		std::uint32_t sid,
		std::uint32_t serial,
		const pt_wz::ZoneWorldAoiDespawnBatch& batch,
		const proto::S2C_player_despawn_item* items,
		std::size_t count)
	{
		auto* handler = lines_.host(WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			spdlog::warn("OnZoneAoiDespawnBatchFromHandler handler missing. zone_sid={} zone_serial={} char_id={}", sid, serial, batch.char_id);
			return;
		}
		if (count > 0 && items != nullptr) {
			std::vector<proto::S2C_player_despawn_item> despawn_items(items, items + count);
			handler->SendPlayerDespawnBatchRelay(0, batch.sid, batch.serial, despawn_items);
		}
		spdlog::info("WorldRuntime relayed zone AOI despawn batch. zone_sid={} zone_serial={} client_sid={} client_serial={} char_id={} map={} channel={} instance={} count={}",
			sid, serial, batch.sid, batch.serial, batch.char_id, batch.map_template_id, batch.channel_id, batch.instance_id, count);
	}

	void WorldRuntime::OnZoneAoiMoveBatchFromHandler(
		std::uint32_t sid,
		std::uint32_t serial,
		const pt_wz::ZoneWorldAoiMoveBatch& batch,
		const proto::S2C_player_move_item* items,
		std::size_t count)
	{
		auto* handler = lines_.host(WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			spdlog::warn("OnZoneAoiMoveBatchFromHandler handler missing. zone_sid={} zone_serial={} char_id={}", sid, serial, batch.char_id);
			return;
		}
		if (count > 0 && items != nullptr) {
			std::vector<proto::S2C_player_move_item> move_items(items, items + count);
			handler->SendPlayerMoveBatchRelay(0, batch.sid, batch.serial, move_items);
		}
		spdlog::info("WorldRuntime relayed zone AOI move batch. zone_sid={} zone_serial={} client_sid={} client_serial={} char_id={} map={} channel={} instance={} count={}",
			sid, serial, batch.sid, batch.serial, batch.char_id, batch.map_template_id, batch.channel_id, batch.instance_id, count);
	}

	void WorldRuntime::SendWorldFallbackZoneSnapshot_(const PendingZoneAoiSnapshotRequest& pending)
	{
		spdlog::warn(
			"WorldRuntime fallback world snapshot suppressed (zone-only mode). sid={} serial={} char_id={} zone_id={} map={} channel={} instance={} reason={}",
			pending.sid,
			pending.serial,
			pending.char_id,
			pending.zone_id,
			pending.map_template_id,
			pending.channel_id,
			pending.instance_id,
			static_cast<int>(pending.reason));
	}

	void WorldRuntime::ExpirePendingZoneAoiSnapshots_(std::chrono::steady_clock::time_point now)
	{
		std::vector<PendingZoneAoiSnapshotRequest> expired;
		{
			std::lock_guard lk(pending_zone_aoi_snapshot_mtx_);
			for (auto it = pending_zone_aoi_snapshot_by_session_key_.begin(); it != pending_zone_aoi_snapshot_by_session_key_.end();) {
				if ((now - it->second.issued_at) < kZoneSnapshotFallbackDelay_) {
					++it;
					continue;
				}
				expired.push_back(it->second);
				it = pending_zone_aoi_snapshot_by_session_key_.erase(it);
			}
		}

		for (const auto& pending : expired) {
			SendWorldFallbackZoneSnapshot_(pending);
		}
	}

	void WorldRuntime::FailPendingPortalTransfer_(
		const PendingPortalTransfer& pending,
		std::string_view reason)
	{
		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (handler) {
			handler->SendZoneMapState(
				0,
				pending.sid,
				pending.serial,
				pending.char_id,
				pending.source_zone_id,
				pending.source_map_template_id,
				pending.source_x,
				pending.source_y,
				proto::ZoneMapStateReason::position_update);
		}

		spdlog::warn(
			"portal transfer failed. sid={} serial={} char_id={} from_zone={} from_map={} to_map={} reason={}",
			pending.sid,
			pending.serial,
			pending.char_id,
			pending.source_zone_id,
			pending.source_map_template_id,
			pending.target_map_template_id,
			reason);
	}

	void WorldRuntime::CompletePortalTransfer_(
		const PendingPortalTransfer& pending,
		std::uint16_t assigned_zone_id,
		std::uint16_t assigned_channel_id,
		std::uint32_t assigned_zone_server_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id)
	{
		auto session = TryGetAuthenticatedWorldSession(pending.sid, pending.serial);
		if (!session.has_value() || session->char_id != pending.char_id) {
			spdlog::warn(
				"portal transfer dropped for stale session. sid={} serial={} char_id={} zone_server={} channel={} map_id={}",
				pending.sid,
				pending.serial,
				pending.char_id,
				assigned_zone_server_id,
				assigned_channel_id,
				map_template_id);
			return;
		}

		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			return;
		}

		const auto source_zone_id = pending.source_zone_id;
		const auto char_id = pending.char_id;
		const auto sid = pending.sid;
		const auto serial = pending.serial;
		const auto target_x = pending.target_x;
		const auto target_y = pending.target_y;

		SendZonePlayerLeave_(pending.source_zone_id, pending.char_id, pending.source_map_template_id, pending.source_instance_id);
		SendZonePlayerEnter_(pending.trace_id, assigned_zone_id, 0, pending.char_id, map_template_id, instance_id);

		PostActor(
			char_id,
			[this, char_id, sid, serial, assigned_zone_id, map_template_id, instance_id, target_x, target_y]() {
				auto& actor = GetOrCreatePlayerActor(char_id);
				actor.SetWorldPosition(assigned_zone_id, map_template_id, instance_id, target_x, target_y);
			});

		PostActor(
			svr::MakeZoneActorId(source_zone_id),
			[this, char_id, sid, serial, source_zone_id, handler]() {
				auto& old_zone = GetOrCreateZoneActor(source_zone_id);
				std::vector<std::pair<std::uint32_t, std::uint32_t>> receivers;
				if (const auto self_it = old_zone.players.find(char_id); self_it != old_zone.players.end()) {
					auto visible_ids = old_zone.GatherNeighborsVec(self_it->second.cx, self_it->second.cy);
					visible_ids.erase(
						std::remove(visible_ids.begin(), visible_ids.end(), char_id),
						visible_ids.end());
					for (const auto other_char_id : visible_ids) {
						auto it = old_zone.players.find(other_char_id);
						if (it == old_zone.players.end()) {
							continue;
						}
						if (it->second.sid != 0 && it->second.serial != 0) {
							receivers.emplace_back(it->second.sid, it->second.serial);
						}
					}
				}

				old_zone.Leave(char_id);
			});

		PostActor(
			svr::MakeZoneActorId(assigned_zone_id),
			[this, pending, char_id, sid, serial, assigned_zone_id, assigned_channel_id, assigned_zone_server_id, map_template_id, instance_id, target_x, target_y, handler]() {
				auto& new_zone = GetOrCreateZoneActor(assigned_zone_id);
				new_zone.JoinOrUpdate(char_id, { target_x, target_y }, sid, serial);

				std::vector<proto::S2C_player_spawn_item> initial_spawn_items;
				std::vector<std::pair<std::uint32_t, std::uint32_t>> remote_receivers;
				if (const auto self_it = new_zone.players.find(char_id); self_it != new_zone.players.end()) {
					auto visible_ids = new_zone.GatherNeighborsVec(self_it->second.cx, self_it->second.cy);
					visible_ids.erase(
						std::remove(visible_ids.begin(), visible_ids.end(), char_id),
						visible_ids.end());
					for (const auto other_char_id : visible_ids) {
						auto it = new_zone.players.find(other_char_id);
						if (it == new_zone.players.end()) {
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
					pending.trace_id,
					sid,
					serial,
					char_id,
					assigned_zone_id,
					map_template_id,
					instance_id,
					target_x,
					target_y,
					ZoneSnapshotReason::portal);

				if (!zone_snapshot_requested) {
					spdlog::warn(
						"portal initial snapshot fallback disabled. sid={} serial={} char_id={} zone_server={} zone_id={} map={} channel={} instance={} would_have_spawn_batch_count={}",
						sid,
						serial,
						char_id,
						assigned_zone_server_id,
						assigned_zone_id,
						map_template_id,
						assigned_channel_id,
						instance_id,
						initial_spawn_items.size());
				}

				spdlog::info(
					"portal initial snapshot prepared. sid={} serial={} char_id={} zone_server={} zone_id={} map={} channel={} instance={} spawn_batch_count={} remote_spawn_notify_count={} zone_snapshot_requested={}",
					sid,
					serial,
					char_id,
					assigned_zone_server_id,
					assigned_zone_id,
					map_template_id,
					assigned_channel_id,
					instance_id,
					initial_spawn_items.size(),
					remote_receivers.size(),
					zone_snapshot_requested ? 1 : 0);
			});

		handler->SendZoneMapState(
			0,
			pending.sid,
			pending.serial,
			pending.char_id,
			assigned_zone_id,
			map_template_id,
			pending.target_x,
			pending.target_y,
			proto::ZoneMapStateReason::portal_moved,
			assigned_channel_id,
			assigned_zone_server_id);
		if (assigned_zone_id != pending.source_zone_id || map_template_id != pending.source_map_template_id) {
			handler->SendZoneMapState(
				0,
				pending.sid,
				pending.serial,
				pending.char_id,
				assigned_zone_id,
				map_template_id,
				pending.target_x,
				pending.target_y,
				proto::ZoneMapStateReason::zone_changed,
				assigned_channel_id,
				assigned_zone_server_id);
		}

		spdlog::info(
			"portal transfer completed. sid={} serial={} char_id={} source_zone={} source_map={} target_zone={} target_map={} target_zone_server={} target_channel={} pos=({}, {})",
			pending.sid,
			pending.serial,
			pending.char_id,
			pending.source_zone_id,
			pending.source_map_template_id,
			assigned_zone_id,
			map_template_id,
			assigned_zone_server_id,
			assigned_channel_id,
			pending.target_x,
			pending.target_y);
	}

	std::optional<ZoneRouteInfo> WorldRuntime::TrySelectZoneRoute_(bool dungeon_instance) const
	{
		std::lock_guard lk(service_line_mtx_);
		std::optional<ZoneRouteInfo> best;
		for (const auto& [sid, route] : zone_routes_by_sid_) {
			if (!route.registered || route.serial == 0 || route.zone_id == 0) {
				continue;
			}

			auto current = MakeZoneRouteInfo_(route);
			const auto current_pressure = ComputeEffectiveRoutePressure_(current);

			if (!best.has_value()) {
				best = current;
				continue;
			}
			const auto best_pressure = ComputeEffectiveRoutePressure_(*best);
			if (current_pressure < best_pressure) {
				best = current;
				continue;
			}
			if (current_pressure == best_pressure && dungeon_instance &&
				current.active_map_instance_count < best->active_map_instance_count) {
				best = current;
				continue;
			}
			if (current_pressure == best_pressure &&
				current.active_player_count < best->active_player_count) {
				best = current;
			}
		}
		return best;
	}

	std::optional<ZoneRouteInfo> WorldRuntime::TrySelectZoneRouteForMap_(std::uint32_t map_template_id, bool dungeon_instance) const
	{
		std::lock_guard lk(service_line_mtx_);
		std::optional<ZoneRouteInfo> best;
		std::vector<std::string> candidates;
		for (const auto& [_, route] : zone_routes_by_sid_) {
			if (!route.registered || route.serial == 0 || route.zone_id == 0) {
				continue;
			}
			if (!RouteServesMap_(route, map_template_id)) {
				continue;
			}

			auto current = MakeZoneRouteInfo_(route);
			const auto pending_assign = CountPendingZoneAssignRequestsBySid_(current.sid, current.serial);
			const auto pending_enter = CountPendingZonePlayerEnterRequestsBySid_(current.sid, current.serial);
			const auto current_pressure = ComputeEffectiveRoutePressure_(current);
			candidates.push_back(fmt::format(
				"sid={} server_id={} zone_id={} channel_id={} load={} pending_assign={} pending_enter={} pressure={} players={} maps={}",
				current.sid,
				current.zone_server_id,
				current.zone_id,
				current.channel_id,
				current.load_score,
				pending_assign,
				pending_enter,
				current_pressure,
				current.active_player_count,
				current.active_map_instance_count));
			if (!best.has_value()) {
				best = current;
				continue;
			}
			const auto best_pressure = ComputeEffectiveRoutePressure_(*best);
			if (current_pressure < best_pressure) {
				best = current;
				continue;
			}
			if (current_pressure == best_pressure &&
				current.active_player_count < best->active_player_count) {
				best = current;
				continue;
			}
			if (current_pressure == best_pressure &&
				current.active_player_count == best->active_player_count &&
				dungeon_instance &&
				current.active_map_instance_count < best->active_map_instance_count) {
				best = current;
				continue;
			}
			if (current_pressure == best_pressure &&
				current.active_player_count == best->active_player_count &&
				current.active_map_instance_count == best->active_map_instance_count &&
				current.channel_id < best->channel_id) {
				best = current;
			}
		}

		if (!candidates.empty()) {
			if (best.has_value()) {
				spdlog::info("route select for map. map_id={} dungeon_instance={} candidates={} selected_server={} selected_zone={} selected_channel={} load={} players={} maps={}",
					map_template_id,
					dungeon_instance ? 1 : 0,
					fmt::join(candidates, " | "),
					best->zone_server_id,
					best->zone_id,
					best->channel_id,
					ComputeEffectiveRoutePressure_(*best),
					best->active_player_count,
					best->active_map_instance_count);
			}
		}
		else {
			spdlog::warn("route select for map failed. map_id={} dungeon_instance={} reason=no_candidates", map_template_id, dungeon_instance ? 1 : 0);
		}
		return best;
	}

	std::uint32_t WorldRuntime::CountPendingZoneAssignRequestsBySid_(std::uint32_t sid, std::uint32_t serial) const
	{
		std::uint32_t count = 0;
		for (const auto& [_, pending] : pending_zone_assign_requests_) {
			if (pending.target_sid == sid && pending.target_serial == serial) {
				++count;
			}
		}
		return count;
	}

	std::uint32_t WorldRuntime::CountPendingZonePlayerEnterRequestsBySid_(std::uint32_t sid, std::uint32_t serial) const
	{
		std::uint32_t count = 0;
		for (const auto& [_, pending] : pending_zone_player_enter_requests_) {
			if (pending.target_sid == sid && pending.target_serial == serial) {
				++count;
			}
		}
		return count;
	}

	std::uint32_t WorldRuntime::ComputeEffectiveRoutePressure_(const ZoneRouteInfo& route) const
	{
		return static_cast<std::uint32_t>(route.load_score) +
			CountPendingZoneAssignRequestsBySid_(route.sid, route.serial) +
			CountPendingZonePlayerEnterRequestsBySid_(route.sid, route.serial);
	}


	std::optional<ZoneRouteInfo> WorldRuntime::FindZoneRouteByZoneId_(std::uint16_t zone_id) const
	{
		std::lock_guard lk(service_line_mtx_);
		for (const auto& [_, route] : zone_routes_by_sid_) {
			if (route.registered && route.zone_id == zone_id) {
				return MakeZoneRouteInfo_(route);
			}
		}
		return std::nullopt;
	}

	std::optional<ZoneRouteInfo> WorldRuntime::FindZoneRouteByServerChannel_(std::uint32_t zone_server_id, std::uint16_t channel_id) const
	{
		std::lock_guard lk(service_line_mtx_);
		for (const auto& [_, route] : zone_routes_by_sid_) {
			if (!route.registered) {
				continue;
			}
			if (route.server_id != zone_server_id) {
				continue;
			}
			if (channel_id != 0 && route.channel_id != channel_id) {
				continue;
			}
			return MakeZoneRouteInfo_(route);
		}
		return std::nullopt;
	}

	std::optional<ZoneRouteInfo> WorldRuntime::ResolveZoneRouteForMapInstance_(
		std::uint16_t zone_id,
		std::uint32_t map_template_id,
		std::uint32_t instance_id) const
	{
		if (const auto it = map_assignments_.find(MakeMapAssignmentKey_(map_template_id, instance_id)); it != map_assignments_.end()) {
			if (auto route = FindZoneRouteByServerChannel_(it->second.zone_server_id, it->second.channel_id); route.has_value()) {
				return route;
			}
		}
		return FindZoneRouteByZoneId_(zone_id);
	}

	bool WorldRuntime::SendZonePlayerEnter_(std::uint64_t trace_id, std::uint16_t zone_id, std::uint64_t request_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id)
	{
		auto route = ResolveZoneRouteForMapInstance_(zone_id, map_template_id, instance_id);
		if (!route.has_value()) {
			return false;
		}
		auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>();
		if (!handler) {
			return false;
		}
		return handler->SendPlayerEnter(0, route->sid, route->serial, trace_id, request_id, char_id, map_template_id, instance_id, zone_id);
	}

	bool WorldRuntime::SendZonePlayerLeave_(std::uint16_t zone_id, std::uint64_t char_id, std::uint32_t map_template_id, std::uint32_t instance_id)
	{
		auto route = ResolveZoneRouteForMapInstance_(zone_id, map_template_id, instance_id);
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
		bool dungeon_instance,
		std::uint64_t trace_id)
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

		if (auto pending_it = pending_zone_assign_request_id_by_map_key_.find(key);
			pending_it != pending_zone_assign_request_id_by_map_key_.end()) {
			result.kind = AssignMapInstanceResultKind::Pending;
			result.request_id = pending_it->second;
			return result;
		}

		auto zone = TrySelectZoneRouteForMap_(map_template_id, dungeon_instance);
		if (!zone.has_value() && !server_topology_.has_value()) {
			zone = TrySelectZoneRoute_(dungeon_instance);
		}
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
		pending.trace_id = trace_id;
		pending.request_id = request_id;
		pending.map_key = key;
		pending.target_sid = zone->sid;
		pending.target_serial = zone->serial;
		pending.target_zone_server_id = zone->zone_server_id;
		pending.target_zone_id = zone->zone_id;
		pending.target_channel_id = zone->channel_id;
		pending.map_template_id = map_template_id;
		pending.instance_id = instance_id;
		pending.issued_at = std::chrono::steady_clock::now();
		pending_zone_assign_requests_[request_id] = pending;
		pending_zone_assign_request_id_by_map_key_[key] = request_id;

		if (!handler->SendMapAssignRequest(0, zone->sid, zone->serial, trace_id, request_id, map_template_id, instance_id, create_if_missing, dungeon_instance)) {
			pending_zone_assign_requests_.erase(request_id);
			pending_zone_assign_request_id_by_map_key_.erase(key);
			result.kind = AssignMapInstanceResultKind::RequestSendFailed;
			return result;
		}

		result.kind = AssignMapInstanceResultKind::Pending;
		result.request_id = request_id;
		result.zone_id = 0;
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

	void WorldRuntime::RemoveMapAssignmentsByZoneSid_(std::uint32_t sid)
	{
		auto route_it = zone_routes_by_sid_.find(sid);
		if (route_it == zone_routes_by_sid_.end()) {
			return;
		}

		const auto zone_server_id = route_it->second.server_id;
		for (auto it = map_assignments_.begin(); it != map_assignments_.end();) {
			if (it->second.zone_server_id == zone_server_id) {
				it = map_assignments_.erase(it);
			}
			else {
				++it;
			}
		}
	}

	void WorldRuntime::FailPendingZoneAssignRequestsByZoneSid_(
		std::uint32_t sid,
		proto::world::EnterWorldResultCode reason,
		std::string_view log_text,
		std::string_view rollback_log)
	{
		for (auto it = pending_zone_assign_requests_.begin(); it != pending_zone_assign_requests_.end();) {
			if (it->second.target_sid != sid) {
				++it;
				continue;
			}

			const auto request_id = it->first;
			pending_zone_assign_request_id_by_map_key_.erase(it->second.map_key);

			auto finalize_it = pending_enter_world_finalize_by_assign_request_.find(request_id);
			if (finalize_it != pending_enter_world_finalize_by_assign_request_.end()) {
				auto finalize_list = std::move(finalize_it->second);
				pending_enter_world_finalize_by_assign_request_.erase(finalize_it);

				for (auto& finalize : finalize_list) {
					if (!IsEnterWorldSessionPending(
						finalize.enter_pending.sid,
						finalize.enter_pending.serial,
						finalize.enter_pending.char_id)) {
						continue;
					}

					RollbackBoundEnterWorld_(finalize.enter_pending, rollback_log);
					FailPendingEnterWorldConsumeRequest_(finalize.enter_pending, reason, log_text);
				}
			}

			auto portal_it = pending_portal_transfer_by_assign_request_.find(request_id);
			if (portal_it != pending_portal_transfer_by_assign_request_.end()) {
				FailPendingPortalTransfer_(portal_it->second, log_text);
				pending_portal_transfer_by_assign_request_.erase(portal_it);
			}

			it = pending_zone_assign_requests_.erase(it);
		}
	}

	void WorldRuntime::FailPendingZonePlayerEnterRequestsByZoneSid_(
		std::uint32_t sid,
		proto::world::EnterWorldResultCode reason,
		std::string_view log_text,
		std::string_view rollback_log)
	{
		for (auto it = pending_zone_player_enter_requests_.begin(); it != pending_zone_player_enter_requests_.end();) {
			if (it->second.target_sid != sid) {
				++it;
				continue;
			}

			auto pending_enter = std::move(it->second);
			it = pending_zone_player_enter_requests_.erase(it);

			if (!IsEnterWorldSessionPending(
				pending_enter.enter_pending.sid,
				pending_enter.enter_pending.serial,
				pending_enter.enter_pending.char_id)) {
				spdlog::warn(
					"FailPendingZonePlayerEnterRequestsByZoneSid_ stale pending enter dropped. zone_sid={} request_id={} world_sid={} world_serial={} char_id={}",
					sid, pending_enter.request_id, pending_enter.enter_pending.sid, pending_enter.enter_pending.serial, pending_enter.enter_pending.char_id);
				continue;
			}

			RollbackBoundEnterWorld_(pending_enter.enter_pending, rollback_log);
			FailPendingEnterWorldConsumeRequest_(pending_enter.enter_pending, reason, log_text);
		}
	}

	std::uint16_t WorldRuntime::GetActiveZoneCount() const
	{
		std::lock_guard lk(service_line_mtx_);
		const auto count = static_cast<std::uint16_t>(zone_routes_by_sid_.size());
		return count == 0 ? 1 : count;
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
		route.served_map_ids.clear();
		if (server_topology_.has_value()) {
			if (const auto* topo = server_topology_->FindZoneProcess(server_id); topo != nullptr) {
				route.zone_id = topo->logical_zone_id;
				route.world_id = topo->world_id;
				route.channel_id = topo->channel_id;
				route.server_name = topo->server_name;
				route.served_map_ids = topo->maps;
			}
		}

		spdlog::info(
			"[{}] sid={} serial={} server_id={} zone_id={} world_id={} channel_id={} active_maps={} active_players={} capacity={} load={} flags={} name={} served_maps={}",
			dc::logevt::world::kZoneRouteReg,
			sid, serial, route.server_id, route.zone_id, route.world_id, route.channel_id, active_map_instance_count, active_player_count, map_instance_capacity, load_score, flags, route.server_name, fmt::join(route.served_map_ids, ","));
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
		if (server_topology_.has_value()) {
			if (const auto* topo = server_topology_->FindZoneProcess(server_id); topo != nullptr) {
				route.zone_id = topo->logical_zone_id;
				route.world_id = topo->world_id;
				route.channel_id = topo->channel_id;
				route.server_name = topo->server_name;
				route.served_map_ids = topo->maps;
			}
		}

		spdlog::debug("[{}] sid={} serial={} zone_id={} channel_id={} active_maps={} active_players={} load={} flags={} served_maps={}",
			dc::logevt::world::kZoneRouteHb, sid, serial, route.zone_id, route.channel_id, active_map_instance_count, active_player_count, load_score, flags, fmt::join(route.served_map_ids, ","));
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
		RemoveMapAssignmentsByZoneSid_(sid);
		FailPendingZoneAssignRequestsByZoneSid_(
			sid,
			pt_w::EnterWorldResultCode::zone_assign_route_lost,
			"UnregisterZoneRoute zone disconnected during map assign.",
			"UnregisterZoneRoute rolled back because zone disconnected during map assign.");
		FailPendingZonePlayerEnterRequestsByZoneSid_(
			sid,
			pt_w::EnterWorldResultCode::zone_player_enter_route_lost,
			"UnregisterZoneRoute zone disconnected during player enter ack.",
			"UnregisterZoneRoute rolled back because zone disconnected during player enter ack.");

		zone_routes_by_sid_.erase(it);
	}



	void WorldRuntime::ExpireStaleZoneRoutes_(std::chrono::steady_clock::time_point now)
	{
		std::vector<std::pair<std::uint32_t, std::uint32_t>> stale_sessions;
		{
			std::lock_guard lk(service_line_mtx_);
			for (auto it = zone_routes_by_sid_.begin(); it != zone_routes_by_sid_.end();) {
				if (it->second.last_heartbeat_at != std::chrono::steady_clock::time_point{} &&
					now - it->second.last_heartbeat_at > dc::k_ExpireStaleZoneRoutes) {
					const auto expired_sid = it->second.sid;
					const auto expired_serial = it->second.serial;

					spdlog::warn(
						"WorldRuntime zone route heartbeat expired. sid={} serial={} server_id={} zone_id={}",
						it->second.sid, it->second.serial, it->second.server_id, it->second.zone_id);

					stale_sessions.emplace_back(expired_sid, expired_serial);

					RemoveMapAssignmentsByZoneSid_(expired_sid);
					FailPendingZoneAssignRequestsByZoneSid_(
						expired_sid,
						pt_w::EnterWorldResultCode::zone_assign_route_lost,
						"ExpireStaleZoneRoutes_ zone route expired during map assign.",
						"ExpireStaleZoneRoutes_ rolled back because zone route expired during map assign.");
					FailPendingZonePlayerEnterRequestsByZoneSid_(
						expired_sid,
						pt_w::EnterWorldResultCode::zone_player_enter_route_lost,
						"ExpireStaleZoneRoutes_ zone route expired during player enter ack.",
						"ExpireStaleZoneRoutes_ rolled back because zone route expired during player enter ack.");
					it = zone_routes_by_sid_.erase(it);
				}
				else {
					++it;
				}
			}

			for (auto it = pending_zone_player_enter_requests_.begin(); it != pending_zone_player_enter_requests_.end();) {
				if (now - it->second.issued_at > dc::k_pending_zone_player_enter_requests) {
					auto pending_enter = std::move(it->second);
					it = pending_zone_player_enter_requests_.erase(it);

					if (!IsEnterWorldSessionPending(
						pending_enter.enter_pending.sid,
						pending_enter.enter_pending.serial,
						pending_enter.enter_pending.char_id)) {
						spdlog::warn(
							"ExpireStaleZoneRoutes_ stale zone-player-enter timeout dropped. request_id={} world_sid={} world_serial={} char_id={}",
							pending_enter.request_id,
							pending_enter.enter_pending.sid,
							pending_enter.enter_pending.serial,
							pending_enter.enter_pending.char_id);
						continue;
					}

					RollbackBoundEnterWorld_(
						pending_enter.enter_pending,
						"ExpireStaleZoneRoutes_ rolled back because zone player enter ack timed out.");

					FailPendingEnterWorldConsumeRequest_(
						pending_enter.enter_pending,
						pt_w::EnterWorldResultCode::zone_player_enter_timeout,
						"ExpireStaleZoneRoutes_ zone player enter ack timeout.");
				}
				else {
					++it;
				}
			}
			for (auto it = pending_portal_transfer_by_assign_request_.begin(); it != pending_portal_transfer_by_assign_request_.end();) {
				if (now - it->second.issued_at > dc::k_pending_portal_transfer_requests) {
					auto pending = it->second;
					it = pending_portal_transfer_by_assign_request_.erase(it);
					FailPendingPortalTransfer_(pending, "portal_transfer_timeout");
				}
				else {
					++it;
				}
			}
		}

		// stale route는 registry에서만 지우면 zone 쪽 reconnect/hello-register가 다시 돌지 않을 수 있다.
		// 따라서 실제 transport close까지 유도해서 zone client가 재연결하도록 만든다.
		if (auto* handler = lines_.host(WorldLineId::Zone).handler_as<WorldZoneHandler>()) {
			for (const auto& [sid, serial] : stale_sessions) {
				spdlog::warn(
					"WorldRuntime force closing stale zone session. sid={} serial={} reason=zone_route_heartbeat_expired",
					sid,
					serial);

				handler->Close(0, sid, serial, false);
			}
		}
		else if (!stale_sessions.empty()) {
			spdlog::warn(
				"WorldRuntime stale zone sessions detected but zone handler is null. count={}",
				stale_sessions.size());
		}
	}

} // namespace svr








