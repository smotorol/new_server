#include "services/world/runtime/world_runtime_private.h"
#include "services/world/common/string_utils.h"

namespace svr {

	using namespace svr::detail;

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
		std::string_view cached_state_blob)
	{
		auto* handler = lines_.host(svr::WorldLineId::World).handler_as<WorldHandler>();
		if (!handler) {
			RollbackBoundEnterWorld_(pending, "FinalizeEnterWorldSuccess_ rolled back because world handler is null.");
			return;
		}

		const auto request_id = next_zone_player_enter_request_id_++;
		PendingZonePlayerEnterRequest pending_enter{};
		pending_enter.request_id = request_id;
		pending_enter.enter_pending = pending;
		pending_enter.target_zone_id = assigned_zone_id;
		pending_enter.map_template_id = map_template_id;
		pending_enter.instance_id = instance_id;
		pending_enter.cached_state_blob.assign(cached_state_blob.begin(), cached_state_blob.end());
		pending_enter.issued_at = std::chrono::steady_clock::now();

		if (auto route = FindZoneRouteByZoneId_(assigned_zone_id); route.has_value()) {
			pending_enter.target_sid = route->sid;
			pending_enter.target_serial = route->serial;
			pending_enter.target_zone_server_id = route->zone_server_id;
		}

		pending_zone_player_enter_requests_[request_id] = pending_enter;

		if (!SendZonePlayerEnter_(assigned_zone_id, request_id, char_id, map_template_id, instance_id)) {
			pending_zone_player_enter_requests_.erase(request_id);
			RollbackBoundEnterWorld_(pending, "FinalizeEnterWorldSuccess_ rolled back because zone player enter send failed.");
			FailPendingEnterWorldConsumeRequest_(
				pending,
				pt_w::EnterWorldResultCode::zone_player_enter_send_failed,
				"FinalizeEnterWorldSuccess_ failed: zone player enter send failed.");
			return;
		}
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

		PostActor(char_id, [this, char_id, sid = pending.sid, serial = pending.serial, assigned_zone_id, map_template_id, instance_id]() {
			auto& a = GetOrCreatePlayerActor(char_id);
			a.bind_session(sid, serial);
			a.combat.hp = 100;
			a.combat.max_hp = 100;
			a.combat.atk = 20;
			a.combat.def = 3;
			a.combat.gold = 1000;
			a.zone_id = assigned_zone_id;
			a.map_template_id = map_template_id;
			a.map_instance_id = instance_id;
			a.pos = { 0, 0 };

			PostActor(svr::MakeZoneActorId(a.zone_id), [this, char_id, sid, serial, assigned_zone_id]() {
				auto& z = GetOrCreateZoneActor(assigned_zone_id);
				z.JoinOrUpdate(char_id, { 0,0 }, sid, serial);
			});
		});

		if (!pending_enter.cached_state_blob.empty()) {
			const std::uint32_t world_code = 0;
			CacheCharacterState(world_code, char_id, pending_enter.cached_state_blob);
		}

		if (!NotifyAccountWorldEnterSuccess(pending.account_id, pending.char_id, pending.login_session, pending.world_token)) {
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
			return;
		}

		proto::S2C_actor_bound bound{};
		bound.actor_id = char_id;
		auto bh = proto::make_header(
			static_cast<std::uint16_t>(proto::S2CMsg::actor_bound),
			static_cast<std::uint16_t>(sizeof(bound)));
		handler->Send(0, pending.sid, pending.serial, bh, reinterpret_cast<const char*>(&bound));

		pt_w::S2C_enter_world_result res{};
		res.ok = 1;
		res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::success);
		res.account_id = pending.account_id;
		res.char_id = pending.char_id;

		const auto h = proto::make_header(
			static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
			static_cast<std::uint16_t>(sizeof(res)));
		handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));
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
			CancelPendingEnterWorldSession(pending.sid, pending.serial, pending.char_id);
			return;
		}

		if (!IsEnterWorldSessionPending(pending.sid, pending.serial, pending.char_id)) {
			spdlog::warn(
				"OnWorldAuthTicketConsumeResponse stale enter pending ignored. request_id={} sid={} serial={} account_id={} char_id={}",
				request_id,
				pending.sid,
				pending.serial,
				pending.account_id,
				pending.char_id);
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
			CancelPendingEnterWorldSession(pending.sid, pending.serial, pending.char_id);
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
			CancelPendingEnterWorldSession(pending.sid, pending.serial, pending.char_id);
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

			switch (map_assignment.kind) {
			case AssignMapInstanceResultKind::Ok:
				FinalizeEnterWorldSuccess_(
					pending,
					account_id,
					char_id,
					pending.login_session,
					pending.world_token,
					map_assignment.zone_id,
					kDefaultMapTemplateId,
					kDefaultInstanceId,
					out_blob);
				return;

			case AssignMapInstanceResultKind::Pending:
				{
					PendingEnterWorldFinalize finalize{};
					finalize.assign_request_id = map_assignment.request_id;
					finalize.enter_pending = pending;
					finalize.map_template_id = kDefaultMapTemplateId;
					finalize.instance_id = kDefaultInstanceId;
					finalize.cached_state_blob = out_blob;

					pending_enter_world_finalize_by_assign_request_[map_assignment.request_id].push_back(std::move(finalize));
					return;
				}

			case AssignMapInstanceResultKind::NoZoneAvailable:
			case AssignMapInstanceResultKind::RequestSendFailed:
			case AssignMapInstanceResultKind::ResponseTimeout:
			case AssignMapInstanceResultKind::Rejected:
			default:
				RollbackBoundEnterWorld_(
					pending,
					"OnWorldAuthTicketConsumeResponse rolled back because map assignment failed.");

				FailPendingEnterWorldConsumeRequest_(
					pending,
					MapAssignFailureToEnterWorldReason_(map_assignment.kind, map_assignment.reject_result_code),
					"OnWorldAuthTicketConsumeResponse map assignment failed.");
				return;
			}
		}

		//const auto h = proto::make_header(
		//	static_cast<std::uint16_t>(pt_w::WorldS2CMsg::enter_world_result),
		//	static_cast<std::uint16_t>(sizeof(res)));
		//handler->Send(0, pending.sid, pending.serial, h, reinterpret_cast<const char*>(&res));

		//spdlog::info(
		//	"OnWorldAuthTicketConsumeResponse success. request_id={} sid={} serial={} account_id={} char_id={} bind_kind={} duplicate_cause={}",
		//	request_id,
		//	pending.sid,
		//	pending.serial,
		//	account_id,
		//	char_id,
		//	ToString(bind_result.kind),
		//	ToString(bind_result.duplicate_cause));
	}



} // namespace svr
