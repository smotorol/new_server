#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/odbc/odbc_wrapper.h"
#include "services/world/common/character_core_state.h"

namespace svr {

	struct CharacterSelectListEntry
	{
		std::uint64_t char_id = 0;
		std::string char_name;
		std::uint32_t level = 0;
		std::uint16_t job = 0;
		std::uint32_t appearance_code = 0;
		std::uint64_t last_login_at_epoch_sec = 0;
	};

	struct CharacterEnterSnapshotLoad
	{
		bool found = false;
		bool cache_blob_applied = false;
		std::string fail_reason;
		CharacterCoreState core_state{};
	};

	class WorldCharacterRepository final
	{
	public:
		static std::vector<CharacterSelectListEntry> LoadCharacterSelectListByAccount(
			db::OdbcConnection& conn,
			std::uint64_t account_id);

		static CharacterEnterSnapshotLoad LoadCharacterEnterSnapshot(
			db::OdbcConnection& conn,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view cached_state_blob);

		static bool FlushCharacterHotState(
			db::OdbcConnection& conn,
			std::uint32_t world_code,
			std::uint64_t char_id,
			const CharacterRuntimeHotState& hot_state,
			std::string_view serialized_blob);
	};

} // namespace svr
