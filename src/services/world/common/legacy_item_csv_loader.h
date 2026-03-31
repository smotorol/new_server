#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "services/world/common/item_template_table.h"

namespace svr {

	struct LegacyItemCsvLoadResult
	{
		bool loaded = false;
		std::vector<ItemTemplate> templates;
		std::size_t invalid_row_count = 0;
		std::size_t duplicate_item_id_count = 0;
		std::filesystem::path source_path;
	};

	LegacyItemCsvLoadResult LoadLegacyItemTemplatesFromCsv(const std::filesystem::path& path);
	LegacyItemCsvLoadResult LoadLegacyItemTemplatesFromCsv();

} // namespace svr
