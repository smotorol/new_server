#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

namespace dc::enterlog {

	enum class EnterStage
	{
		ClientLoginRequestReceived,
		LoginAccountAuthRequestSent,
		AccountAuthRequestReceived,
		AccountAuthResultReceived,
		CharacterListRequestReceived,
		CharacterListResponseSent,
		CharacterSelectRequestReceived,
		CharacterSelectSuccess,
		CharacterSelectFailed,
		CharacterSelectionRejected,
		WorldTicketIssued,
		ClientWorldEnterRequestReceived,
		WorldTokenConsumeRequested,
		WorldTokenConsumeResult,
		WorldCharacterSnapshotLoadRequested,
		WorldCharacterSnapshotLoadResult,
		WorldZoneAssignRequestSent,
		WorldZoneAssignResult,
		ZoneAssignRequestReceived,
		ZonePlayerEnterRequestReceived,
		ZonePlayerEnterResult,
		WorldSessionBound,
		WorldEnterSuccessNotifySent,
		LoginWorldEnterNotifyReceived,
		EnterFlowAborted,
	};

	struct EnterTraceContext
	{
		std::uint64_t trace_id = 0;
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::string_view login_session{};
		std::string_view world_token{};
	};

	inline const char* ToString(EnterStage stage) noexcept
	{
		switch (stage) {
		case EnterStage::ClientLoginRequestReceived: return "client_login_request_received";
		case EnterStage::LoginAccountAuthRequestSent: return "login_account_auth_request_sent";
		case EnterStage::AccountAuthRequestReceived: return "account_auth_request_received";
		case EnterStage::AccountAuthResultReceived: return "account_auth_result_received";
		case EnterStage::CharacterListRequestReceived: return "character_list_request_received";
		case EnterStage::CharacterListResponseSent: return "character_list_response_sent";
		case EnterStage::CharacterSelectRequestReceived: return "character_select_request_received";
		case EnterStage::CharacterSelectSuccess: return "character_select_success";
		case EnterStage::CharacterSelectFailed: return "character_select_failed";
		case EnterStage::CharacterSelectionRejected: return "character_selection_rejected";
		case EnterStage::WorldTicketIssued: return "world_ticket_issued";
		case EnterStage::ClientWorldEnterRequestReceived: return "client_world_enter_request_received";
		case EnterStage::WorldTokenConsumeRequested: return "world_token_consume_requested";
		case EnterStage::WorldTokenConsumeResult: return "world_token_consume_result";
		case EnterStage::WorldCharacterSnapshotLoadRequested: return "world_character_snapshot_load_requested";
		case EnterStage::WorldCharacterSnapshotLoadResult: return "world_character_snapshot_load_result";
		case EnterStage::WorldZoneAssignRequestSent: return "world_zone_assign_request_sent";
		case EnterStage::WorldZoneAssignResult: return "world_zone_assign_result";
		case EnterStage::ZoneAssignRequestReceived: return "zone_assign_request_received";
		case EnterStage::ZonePlayerEnterRequestReceived: return "zone_player_enter_request_received";
		case EnterStage::ZonePlayerEnterResult: return "zone_player_enter_result";
		case EnterStage::WorldSessionBound: return "world_session_bound";
		case EnterStage::WorldEnterSuccessNotifySent: return "world_enter_success_notify_sent";
		case EnterStage::LoginWorldEnterNotifyReceived: return "login_world_enter_notify_received";
		case EnterStage::EnterFlowAborted: return "enter_flow_aborted";
		default: return "unknown";
		}
	}

	inline std::string TokenHint(std::string_view token)
	{
		if (token.empty()) {
			return "-";
		}
		if (token.size() <= 8) {
			return std::string(token);
		}
		return std::string(token.substr(token.size() - 8));
	}

	inline void LogEnterFlow(
		spdlog::level::level_enum level,
		EnterStage stage,
		const EnterTraceContext& ctx,
		std::string_view reason = {},
		std::string_view note = {})
	{
		spdlog::log(
			level,
			"[enter_flow] trace_id={} stage={} account_id={} char_id={} sid={} serial={} login_session_present={} world_token_present={} login_session_hint={} world_token_hint={} reason={} note={}",
			ctx.trace_id,
			ToString(stage),
			ctx.account_id,
			ctx.char_id,
			ctx.sid,
			ctx.serial,
			!ctx.login_session.empty(),
			!ctx.world_token.empty(),
			TokenHint(ctx.login_session),
			TokenHint(ctx.world_token),
			reason.empty() ? "-" : reason,
			note.empty() ? "-" : note);
	}

} // namespace dc::enterlog
