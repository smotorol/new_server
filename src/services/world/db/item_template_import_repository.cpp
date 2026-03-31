#include "services/world/db/item_template_import_repository.h"

#include <sstream>

namespace svr {

	namespace {

		std::string ToSqlString_(const ItemTemplate& item)
		{
			std::ostringstream oss;
			oss
				<< "SELECT TOP (1) [equip_part], [equip_tribe], [attack], [defense], [life], [mana], [vitality], [ki], [is_deleted], [source_tag] "
				<< "FROM [NFX_GAME].[game].[item_template] WHERE [item_id] = " << item.item_id;
			return oss.str();
		}

		std::string BuildInsertSql_(const ItemTemplate& item, std::string_view source_tag)
		{
			std::ostringstream oss;
			oss
				<< "INSERT INTO [NFX_GAME].[game].[item_template] "
				<< "([item_id], [equip_part], [equip_tribe], [attack], [defense], [life], [mana], [vitality], [ki], [is_deleted], [source_tag]) VALUES ("
				<< item.item_id << ", "
				<< item.equip_part << ", "
				<< item.equip_tribe << ", "
				<< item.attack << ", "
				<< item.defense << ", "
				<< item.life << ", "
				<< item.mana << ", "
				<< item.vitality << ", "
				<< item.ki << ", 0, '" << source_tag << "')";
			return oss.str();
		}

		std::string BuildUpdateSql_(const ItemTemplate& item, std::string_view source_tag)
		{
			std::ostringstream oss;
			oss
				<< "UPDATE [NFX_GAME].[game].[item_template] SET "
				<< "[equip_part] = " << item.equip_part << ", "
				<< "[equip_tribe] = " << item.equip_tribe << ", "
				<< "[attack] = " << item.attack << ", "
				<< "[defense] = " << item.defense << ", "
				<< "[life] = " << item.life << ", "
				<< "[mana] = " << item.mana << ", "
				<< "[vitality] = " << item.vitality << ", "
				<< "[ki] = " << item.ki << ", "
				<< "[is_deleted] = 0, "
				<< "[source_tag] = '" << source_tag << "', "
				<< "[updated_at_utc] = SYSUTCDATETIME() "
				<< "WHERE [item_id] = " << item.item_id;
			return oss.str();
		}

	} // namespace

	bool ItemTemplateImportRepository::UpsertItemTemplate(
		db::OdbcConnection& conn,
		const ItemTemplate& item,
		std::string_view source_tag,
		ItemTemplateImportStats& stats)
	{
		if (item.item_id == 0) {
			++stats.invalid;
			return false;
		}

		db::OdbcStatement stmt(conn);
		stmt.prepare(ToSqlString_(item));
		stmt.execute();

		if (!stmt.fetch()) {
			conn.execute(BuildInsertSql_(item, source_tag));
			++stats.inserted;
			return true;
		}

		const auto existing_equip_part = stmt.get_int(1);
		const auto existing_equip_tribe = stmt.get_int(2);
		const auto existing_attack = stmt.get_int(3);
		const auto existing_defense = stmt.get_int(4);
		const auto existing_life = stmt.get_int(5);
		const auto existing_mana = stmt.get_int(6);
		const auto existing_vitality = stmt.get_int(7);
		const auto existing_ki = stmt.get_int(8);
		const auto existing_is_deleted = stmt.get_int(9);
		const auto existing_source_tag = stmt.get_string(10);

		if (existing_equip_part == item.equip_part &&
			existing_equip_tribe == item.equip_tribe &&
			existing_attack == item.attack &&
			existing_defense == item.defense &&
			existing_life == item.life &&
			existing_mana == item.mana &&
			existing_vitality == item.vitality &&
			existing_ki == item.ki &&
			existing_is_deleted == 0 &&
			existing_source_tag == source_tag) {
			++stats.skipped;
			return true;
		}

		conn.execute(BuildUpdateSql_(item, source_tag));
		++stats.updated;
		return true;
	}

} // namespace svr

