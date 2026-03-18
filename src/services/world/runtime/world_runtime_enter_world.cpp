#include "services/world/runtime/world_runtime_private.h"
#include "services/world/common/string_utils.h"

namespace svr {

using namespace svr::detail;


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



	void WorldRuntime::RollbackBoundEnterWorld_(
		const PendingEnterWorldConsumeRequest& pending,
		std::string_view reason_log)
	{
		const auto rollback = UnbindAuthenticatedWorldSessionBySid(pending.sid, pending.serial);

		ClosedAuthedSessionContext rollback_ctx{};
		rollback_ctx.unbind_kind = rollback.kind;
		rollback_ctx.account_id = rollback.session.account_id;
		rollback_ctx.char_id = rollback.session.char_id;
		rollback_ctx.sid = pending.sid;
		rollback_ctx.serial = pending.serial;

		CleanupClosedWorldSessionActors_(rollback_ctx);

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

		if (!SendZonePlayerEnter_(assigned_zone_id, char_id, map_template_id, instance_id)) {
			RollbackBoundEnterWorld_(pending, "FinalizeEnterWorldSuccess_ rolled back because zone player enter send failed.");
			FailPendingEnterWorldConsumeRequest_(
				pending,
				pt_w::EnterWorldResultCode::internal_error,
				"FinalizeEnterWorldSuccess_ failed: zone player enter send failed.");
			return;
		}

		if (!cached_state_blob.empty()) {
			const std::uint32_t world_code = 0;
			CacheCharacterState(world_code, char_id, std::string(cached_state_blob));
		}

		proto::S2C_actor_bound bound{};
		bound.actor_id = char_id;
		auto bh = proto::make_header(
			static_cast<std::uint16_t>(proto::S2CMsg::actor_bound),
			static_cast<std::uint16_t>(sizeof(bound)));
		handler->Send(0, pending.sid, pending.serial, bh, reinterpret_cast<const char*>(&bound));

		if (!NotifyAccountWorldEnterSuccess(account_id, char_id, login_session, world_token)) {
			RollbackBoundEnterWorld_(pending, "FinalizeEnterWorldSuccess_ rolled back because account world_enter_success_notify send failed.");
			FailPendingEnterWorldConsumeRequest_(
				pending,
				pt_w::EnterWorldResultCode::internal_error,
				"FinalizeEnterWorldSuccess_ failed: account world_enter_success_notify send failed.");
			return;
		}

		pt_w::S2C_enter_world_result res{};
		res.ok = 1;
		res.reason = static_cast<std::uint16_t>(pt_w::EnterWorldResultCode::success);
		res.account_id = account_id;
		res.char_id = char_id;

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
					pt_w::EnterWorldResultCode::internal_error,
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
