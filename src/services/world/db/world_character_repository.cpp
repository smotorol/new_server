#include "services/world/db/world_character_repository.h"

#include <array>
#include <exception>
#include <vector>

#include <spdlog/spdlog.h>

#include "services/world/common/demo_char_state.h"

namespace svr {

	namespace {

		struct CharacterRow final
		{
			std::uint64_t char_id = 0;
			std::uint64_t account_id = 0;
			std::string char_name;
			std::uint32_t level = 1;
			std::uint16_t job = 0;
			std::uint16_t tribe = 0;
			std::uint32_t appearance_code = 0;
			std::uint64_t last_login_at_epoch_sec = 0;
		};

		struct CharacterEquipSlots final
		{
			std::array<std::uint32_t, 8> equip_item_ids{};
		};

		std::vector<CharacterRow> QueryCharactersByAccountId_(
			db::OdbcConnection& conn,
			std::uint64_t account_id)
		{
			std::vector<CharacterRow> rows;
			db::OdbcStatement stmt(conn);

			stmt.prepare(
				"SELECT TOP (8) [char_id], [account_id], [char_name], [level], [job], [tribe], [appearance_code], [last_login_at_utc] "
				"FROM [NFX_GAME].[game].[character] "
				"WHERE [account_id] = ? "
				"  AND [char_state] = 1 "
				"ORDER BY [slot_no] ASC, [char_id] ASC");

			stmt.bind_input_uint64(1, account_id);
			stmt.execute();

			while (stmt.fetch()) {
				CharacterRow row{};
				row.char_id = stmt.get_uint64(1);
				row.account_id = stmt.get_uint64(2);
				row.char_name = stmt.get_string(3);
				row.level = static_cast<std::uint32_t>(stmt.get_int(4));
				row.job = static_cast<std::uint16_t>(stmt.get_int(5));
				row.tribe = static_cast<std::uint16_t>(stmt.get_int(6));
				row.appearance_code = static_cast<std::uint32_t>(stmt.get_int(7));
				rows.push_back(std::move(row));
			}

			return rows;
		}

		std::optional<CharacterRow> QueryCharacterById_(
			db::OdbcConnection& conn,
			std::uint64_t char_id)
		{
			db::OdbcStatement stmt(conn);

			stmt.prepare(
				"SELECT TOP (1) [char_id], [account_id], [char_name], [level], [job], [tribe], [appearance_code], [last_login_at_utc] "
				"FROM [NFX_GAME].[game].[character] "
				"WHERE [char_id] = ? "
				"  AND [char_state] = 1");

			stmt.bind_input_uint64(1, char_id);
			stmt.execute();

			if (!stmt.fetch()) {
				return std::nullopt;
			}

			CharacterRow row{};
			row.char_id = stmt.get_uint64(1);
			row.account_id = stmt.get_uint64(2);
			row.char_name = stmt.get_string(3);
			row.level = static_cast<std::uint32_t>(stmt.get_int(4));
			row.job = static_cast<std::uint16_t>(stmt.get_int(5));
			row.tribe = static_cast<std::uint16_t>(stmt.get_int(6));
			row.appearance_code = static_cast<std::uint32_t>(stmt.get_int(7));
			return row;
		}

		CharacterEquipSlots QueryCharacterEquipSlots_(
			db::OdbcConnection& conn,
			std::uint64_t char_id)
		{
			CharacterEquipSlots out{};
			db::OdbcStatement stmt(conn);

			stmt.prepare(
				"SELECT [slot_no], [item_id] "
				"FROM [NFX_GAME].[game].[character_item] "
				"WHERE [char_id] = ? "
				"  AND [slot_type] = 2 "
				"  AND [is_deleted] = 0 "
				"  AND [slot_no] BETWEEN 0 AND 7");

			stmt.bind_input_uint64(1, char_id);
			stmt.execute();

			while (stmt.fetch()) {
				const auto slot_no = stmt.get_int(1);
				if (slot_no < 0 || slot_no >= static_cast<int>(out.equip_item_ids.size())) {
					continue;
				}

				out.equip_item_ids[static_cast<std::size_t>(slot_no)] =
					static_cast<std::uint32_t>(stmt.get_int(2));
			}

			return out;
		}

		EquipSummary BuildEquipSummary_(const CharacterEquipSlots& slots) noexcept
		{
			EquipSummary out{};

			// Legacy TS_ZONE uses aEquip[0] as weapon and aEquip[1..7] as visible armor/accessory slots.
			out.weapon_template_id = slots.equip_item_ids[0];
			for (std::size_t i = 1; i <= 5; ++i) {
				if (slots.equip_item_ids[i] != 0) {
					out.armor_template_id = slots.equip_item_ids[i];
					break;
				}
			}
			for (std::size_t i = 6; i <= 7; ++i) {
				if (slots.equip_item_ids[i] != 0) {
					out.accessory_template_id = slots.equip_item_ids[i];
					break;
				}
			}
			return out;
		}

	} // namespace

	std::vector<CharacterSelectListEntry> WorldCharacterRepository::LoadCharacterSelectListByAccount(
		db::OdbcConnection& conn,
		std::uint64_t account_id)
	{
		std::vector<CharacterSelectListEntry> out;
		for (const auto& row : QueryCharactersByAccountId_(conn, account_id)) {
			CharacterSelectListEntry entry{};
			entry.char_id = row.char_id;
			entry.char_name = row.char_name;
			entry.level = row.level;
			entry.job = row.job;
			entry.appearance_code = row.appearance_code;
			entry.last_login_at_epoch_sec = row.last_login_at_epoch_sec;
			out.push_back(std::move(entry));
		}
		return out;
	}

	CharacterEnterSnapshotLoad WorldCharacterRepository::LoadCharacterEnterSnapshot(
		db::OdbcConnection& conn,
		std::uint64_t account_id,
		std::uint64_t char_id,
		std::string_view cached_state_blob)
	{
		CharacterEnterSnapshotLoad out{};

		try {
			const auto row = QueryCharacterById_(conn, char_id);
			if (!row.has_value()) {
				out.fail_reason = "character_not_found";
				return out;
			}

			out.found = true;
			out.core_state = MakeDefaultCharacterCoreState(row->account_id, row->char_id);
			out.core_state.identity.account_id = row->account_id;
			out.core_state.identity.char_id = row->char_id;
			out.core_state.identity.char_name = row->char_name;
			out.core_state.identity.level = row->level;
			out.core_state.identity.job = row->job;
			out.core_state.identity.tribe = row->tribe;
			out.core_state.appearance.appearance_code = row->appearance_code;
			out.core_state.equip = BuildEquipSummary_(QueryCharacterEquipSlots_(conn, row->char_id));

			if (!cached_state_blob.empty()) {
				CharacterCoreState loaded{};
				if (demo::TryDeserializeDemo(std::string(cached_state_blob), loaded, row->account_id) &&
					loaded.identity.char_id == row->char_id) {
					out.cache_blob_applied = true;
					out.core_state.hot.resources.gold = loaded.hot.resources.gold;
					out.core_state.hot.version = loaded.hot.version;
				}
			}

			if (account_id != 0 && row->account_id != account_id) {
				out.fail_reason = "account_char_mismatch";
				out.found = false;
				out.core_state = {};
			}

			return out;
		}
		catch (const std::exception& e) {
			spdlog::error("WorldCharacterRepository::LoadCharacterEnterSnapshot exception: {}", e.what());
			out.fail_reason = "db_error";
			return out;
		}
	}

	bool WorldCharacterRepository::FlushCharacterHotState(
		db::OdbcConnection& conn,
		std::uint32_t /*world_code*/,
		std::uint64_t char_id,
		const CharacterRuntimeHotState& /*hot_state*/,
		std::string_view serialized_blob)
	{
		if (char_id == 0 || serialized_blob.empty()) {
			return false;
		}

		db::save_character_blob(conn, char_id, std::string(serialized_blob));
		return true;
	}

} // namespace svr
