#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "db/odbc/odbc_wrapper.h"
#include "services/world/common/item_template_table.h"

namespace svr {

	struct ItemTemplateRepositoryLoadResult
	{
		bool loaded = false;
		bool fallback_used = false;
		std::size_t loaded_count = 0;
		std::size_t duplicate_item_id_count = 0;
		std::size_t invalid_row_count = 0;
		std::string source;
		std::string note;
	};

	struct ItemTemplateRepositoryStatus
	{
		bool ready = false;
		bool loaded = false;
		bool empty = true;
		bool fallback_allowed = true;
		bool fallback_entered = false;
		std::size_t preload_count = 0;
		std::uint64_t miss_count = 0;
		std::string source = "empty";
		std::string last_error_reason;
		std::vector<std::uint32_t> miss_samples{};
	};

	class ItemTemplateRepository final
	{
	public:
		static ItemTemplateRepositoryLoadResult LoadCanonicalTableFromDb(db::OdbcConnection& conn);
		static ItemTemplateRepositoryLoadResult BootstrapCanonicalTableFromLegacyCsv();
		static const ItemTemplate* Find(std::uint32_t item_id) noexcept;
		static bool Empty() noexcept;
		static bool IsLoaded() noexcept;
		static void SetLegacyFallbackAllowed(bool allowed) noexcept;
		static bool LegacyFallbackAllowed() noexcept;
		static void RecordTemplateMiss(std::uint32_t item_id) noexcept;
		static ItemTemplateRepositoryStatus SnapshotStatus();
	};

} // namespace svr