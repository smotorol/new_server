#pragma once

#include <cstdint>
#include <variant>

#include "db/core/dqs_types.h"
#include "proto/internal/login_account_proto.h"
#include "services/world/common/character_core_state.h"

namespace pt_la = proto::internal::login_account;

namespace svr::dqs_result {

	// 1) 클라 요청 기반 결과(예: open_world_notice)
	struct OpenWorldNoticeResult final {
		std::uint32_t world_code = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		int ok = 0; // 기존 로직 유지(SELECT 1 결과 등)
		svr::dqs::ResultCode result = svr::dqs::ResultCode::success;

	};

	// 2) 주기 flush 결과
	struct FlushDirtyCharsResult final {
		std::uint32_t world_code = 0;
		std::uint32_t shard_id = 0;
		std::uint32_t max_batch = 0;
		std::uint32_t pulled = 0;   // dirty set에서 뽑은 개수
		std::uint32_t saved = 0;    // DB 저장 성공
		std::uint32_t failed = 0;   // DB 저장 실패
		std::uint32_t conflicts = 0; // 버전 충돌로 저장 스킵
		svr::dqs::ResultCode result = svr::dqs::ResultCode::success;

	};

	// 3) 특정 캐릭터 즉시 flush 결과(로그아웃 등)
	struct FlushOneCharResult final {
		std::uint32_t world_code = 0;
		std::uint64_t char_id = 0;
		std::uint32_t expected_version = 0;
		std::uint32_t actual_version = 0;
		bool saved = false;
		svr::dqs::ResultCode result = svr::dqs::ResultCode::success;

	};

	struct WorldCharacterEnterSnapshotResult final {
		std::uint32_t world_code = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
		bool found = false;
		bool cache_blob_applied = false;
		svr::CharacterCoreState core_state{};
		char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
		svr::dqs::ResultCode result = svr::dqs::ResultCode::success;
	};

	struct AccountAuthResult final {
		std::uint64_t trace_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t request_id = 0;
		std::uint8_t ok = 0;
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
		char login_session[dc::k_login_session_max_len + 1]{};
		char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
		svr::dqs::ResultCode result = svr::dqs::ResultCode::success;
	};

	struct AccountCharacterSummary final {
		std::uint64_t char_id = 0;
		char char_name[dc::k_character_name_max_len + 1]{};
		std::uint32_t level = 0;
		std::uint16_t job = 0;
		std::uint32_t appearance_code = 0;
		std::uint64_t last_login_at_epoch_sec = 0;
	};

	struct AccountCharacterListResult final {
		std::uint64_t trace_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t request_id = 0;
		std::uint64_t account_id = 0;
		std::uint8_t ok = 0;
		std::uint16_t count = 0;
		char fail_reason[dc::k_auth_fail_reason_max_len + 1]{};
		AccountCharacterSummary characters[dc::k_character_list_max_count]{};
		svr::dqs::ResultCode result = svr::dqs::ResultCode::success;
	};

	using Result = std::variant<
		OpenWorldNoticeResult,
		FlushDirtyCharsResult,
		FlushOneCharResult,
		WorldCharacterEnterSnapshotResult,
		AccountAuthResult,
		AccountCharacterListResult>;


} // namespace svr::dqs_result
