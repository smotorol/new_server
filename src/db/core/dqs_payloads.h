#pragma once
#include <cstdint>
#include <cstring>

#include "shared/constants.h"

namespace svr::dqs_payload {

#pragma pack(push, 1)
	// DQS payload 샘플: 클라이언트의 open_world_notice를 DB에서 검증 후 응답 보내기
	// slot.data 맨 앞 4바이트(world_code)는 기존 호환을 위해 유지
	struct OpenWorldNotice final
	{
		std::uint32_t world_code = 0; // 기존 read_world_code() 호환
		std::uint32_t sid = 0;        // 응답 보낼 세션 index
		std::uint32_t serial = 0;     // 응답 안전성(serial 체크)
		char world_name[33]{};        // proto::max_world_name_len(32)+1

		OpenWorldNotice() { std::memset(this, 0, sizeof(*this)); }
	};

	// 주기 flush: world_code의 dirty set에서 최대 batch 개를 DB로 반영
	struct FlushDirtyChars final
	{
		std::uint32_t world_code = 0;
		std::uint32_t shard_id = 0; // ✅ shard별 dirty set을 flush
		std::uint32_t max_batch = 0;
		FlushDirtyChars() { std::memset(this, 0, sizeof(*this)); }
	};

	// 로그아웃 flush: 특정 캐릭터 하나를 즉시 DB로 반영
	struct FlushOneChar final
	{
		std::uint32_t world_code = 0;
		std::uint64_t char_id = 0;
		std::uint32_t expected_version = 0; // 0 means "no version check"
		FlushOneChar() { std::memset(this, 0, sizeof(*this)); }
	};

	struct AccountAuthRequest final
	{
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t request_id = 0;
		std::uint64_t selected_char_id = 0;
		char login_id[dc::k_login_id_max_len + 1]{};
		char password[dc::k_login_pw_max_len + 1]{};

		AccountAuthRequest() { std::memset(this, 0, sizeof(*this)); }
	};
#pragma pack(pop)


} // namespace logsvr::dqs_payload
