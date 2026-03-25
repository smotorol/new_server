#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "db/odbc/odbc_wrapper.h"
#include "services/world/common/item_template_table.h"

namespace svr {

	struct ItemTemplateImportStats
	{
		std::size_t inserted = 0;
		std::size_t updated = 0;
		std::size_t skipped = 0;
		std::size_t invalid = 0;
		std::size_t duplicates = 0;
		std::size_t source_rows = 0;
	};

	class ItemTemplateImportRepository final
	{
	public:
		static bool UpsertItemTemplate(
			db::OdbcConnection& conn,
			const ItemTemplate& item,
			std::string_view source_tag,
			ItemTemplateImportStats& stats);
	};

} // namespace svr
