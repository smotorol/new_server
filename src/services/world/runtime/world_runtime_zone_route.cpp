#include "services/world/runtime/world_runtime_private.h"

namespace svr {

	namespace {
		static bool IsZoneAssignOk_(std::uint16_t result_code) noexcept
		{
			return result_code == static_cast<std::uint16_t>(pt_wz::ZoneMapAssignResultCode::ok);
		}
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
				RollbackBoundEnterWorld_(
					finalize.enter_pending,
					"OnZoneMapAssignResponse rolled back because response map key mismatched.");

				FailPendingEnterWorldConsumeRequest_(
					finalize.enter_pending,
					pt_w::EnterWorldResultCode::internal_error,
					"OnZoneMapAssignResponse map key mismatch.");
			}
			return;
		}

		if (!IsZoneAssignOk_(res.result_code)) {
			for (auto& finalize : finalize_list) {
				RollbackBoundEnterWorld_(
					finalize.enter_pending,
					"OnZoneMapAssignResponse rolled back because zone rejected map assign.");

				FailPendingEnterWorldConsumeRequest_(
					finalize.enter_pending,
					pt_w::EnterWorldResultCode::internal_error,
					"OnZoneMapAssignResponse rejected.");
			}
			return;
		}

		map_assignments_[MakeMapAssignmentKey_(res.map_template_id, res.instance_id)] =
			MapAssignmentEntry{
				pending_assign.target_zone_server_id,
				res.zone_id,
				res.map_template_id,
				res.instance_id
		};

		for (auto& finalize : finalize_list) {
			FinalizeEnterWorldSuccess_(
				finalize.enter_pending,
				finalize.enter_pending.account_id,
				finalize.enter_pending.char_id,
				finalize.enter_pending.login_session,
				finalize.enter_pending.world_token,
				res.zone_id,
				finalize.map_template_id,
				finalize.instance_id,
				finalize.cached_state_blob);
		}
	}





	std::optional<ZoneRouteInfo> WorldRuntime::TrySelectZoneRoute_(bool dungeon_instance) const
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



	std::optional<ZoneRouteInfo> WorldRuntime::FindZoneRouteByZoneId_(std::uint16_t zone_id) const
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

		if (auto pending_it = pending_zone_assign_request_id_by_map_key_.find(key);
			pending_it != pending_zone_assign_request_id_by_map_key_.end()) {
			result.kind = AssignMapInstanceResultKind::Pending;
			result.request_id = pending_it->second;
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
		pending.map_key = key;
		pending.target_sid = zone->sid;
		pending.target_serial = zone->serial;
		pending.target_zone_server_id = zone->zone_server_id;
		pending.target_zone_id = zone->zone_id;
		pending.map_template_id = map_template_id;
		pending.instance_id = instance_id;
		pending.issued_at = std::chrono::steady_clock::now();
		pending_zone_assign_requests_[request_id] = pending;
		pending_zone_assign_request_id_by_map_key_[key] = request_id;

		if (!handler->SendMapAssignRequest(0, zone->sid, zone->serial, request_id, map_template_id, instance_id, create_if_missing, dungeon_instance)) {
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
					RollbackBoundEnterWorld_(finalize.enter_pending, rollback_log);
					FailPendingEnterWorldConsumeRequest_(finalize.enter_pending, reason, log_text);
				}
			}

			it = pending_zone_assign_requests_.erase(it);
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
		RemoveMapAssignmentsByZoneSid_(sid);
		FailPendingZoneAssignRequestsByZoneSid_(
			sid,
			pt_w::EnterWorldResultCode::internal_error,
			"UnregisterZoneRoute zone disconnected during map assign.",
			"UnregisterZoneRoute rolled back because zone disconnected during map assign.");
		zone_routes_by_sid_.erase(it);
	}



	void WorldRuntime::ExpireStaleZoneRoutes_(std::chrono::steady_clock::time_point now)
	{
		std::lock_guard lk(service_line_mtx_);
		for (auto it = zone_routes_by_sid_.begin(); it != zone_routes_by_sid_.end();) {
			if (it->second.last_heartbeat_at != std::chrono::steady_clock::time_point{} &&
				now - it->second.last_heartbeat_at > std::chrono::seconds(8)) {
				const auto expired_sid = it->second.sid;

				spdlog::warn(
					"WorldRuntime zone route heartbeat expired. sid={} serial={} server_id={} zone_id={}",
					it->second.sid, it->second.serial, it->second.server_id, it->second.zone_id);
				RemoveMapAssignmentsByZoneSid_(expired_sid);
				FailPendingZoneAssignRequestsByZoneSid_(
					expired_sid,
					pt_w::EnterWorldResultCode::internal_error,
					"ExpireStaleZoneRoutes_ zone route expired during map assign.",
					"ExpireStaleZoneRoutes_ rolled back because zone route expired during map assign.");
				it = zone_routes_by_sid_.erase(it);
			}
			else {
				++it;
			}
		}

		for (auto it = pending_zone_assign_requests_.begin(); it != pending_zone_assign_requests_.end();) {
			if (now - it->second.issued_at > std::chrono::seconds(3)) {
				const auto request_id = it->first;
				pending_zone_assign_request_id_by_map_key_.erase(it->second.map_key);

				auto finalize_it = pending_enter_world_finalize_by_assign_request_.find(request_id);
				if (finalize_it != pending_enter_world_finalize_by_assign_request_.end()) {
					auto finalize_list = std::move(finalize_it->second);
					pending_enter_world_finalize_by_assign_request_.erase(finalize_it);

					for (auto& finalize : finalize_list) {
						RollbackBoundEnterWorld_(
							finalize.enter_pending,
							"ExpireStaleZoneRoutes_ rolled back because zone assign timed out.");

						FailPendingEnterWorldConsumeRequest_(
							finalize.enter_pending,
							pt_w::EnterWorldResultCode::internal_error,
							"ExpireStaleZoneRoutes_ zone assign timeout.");
					}
				}

				it = pending_zone_assign_requests_.erase(it);
			}
			else {
				++it;
			}
		}
	}
} // namespace svr
