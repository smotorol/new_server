#include "services/world/common/legacy_item_csv_loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

namespace svr {

	namespace {

		constexpr const char* kDefaultLegacyTsZoneDataDir =
			"G:\\Programing\\Work\\12sky1\\12sky1\\S07_TS_ZONE\\DATA";

		std::vector<std::string> SplitTab_(std::string_view line)
		{
			std::vector<std::string> out;
			std::size_t start = 0;
			while (start <= line.size()) {
				const auto pos = line.find('\t', start);
				if (pos == std::string_view::npos) {
					out.emplace_back(line.substr(start));
					break;
				}
				out.emplace_back(line.substr(start, pos - start));
				start = pos + 1;
			}
			return out;
		}

		std::uint32_t ParseUInt_(std::string_view value) noexcept
		{
			if (value.empty()) {
				return 0;
			}
			try {
				return static_cast<std::uint32_t>(std::stoul(std::string(value)));
			}
			catch (...) {
				return 0;
			}
		}

		std::filesystem::path ResolveLegacyDataDir_()
		{
			if (const char* override_dir = std::getenv("DC_LEGACY_TS_ZONE_DATA_DIR");
				override_dir != nullptr && override_dir[0] != '\0') {
				return std::filesystem::path(override_dir);
			}
			return std::filesystem::path(kDefaultLegacyTsZoneDataDir);
		}

	} // namespace

	LegacyItemCsvLoadResult LoadLegacyItemTemplatesFromCsv(const std::filesystem::path& path)
	{
		LegacyItemCsvLoadResult out{};
		out.source_path = path;
		std::unordered_set<std::uint32_t> seen_item_ids;

		std::ifstream file(path);
		if (!file.is_open()) {
			spdlog::warn("Legacy ITEM csv not found: {}. Canonical item template table bootstrap will stay empty.", path.string());
			return out;
		}

		std::string line;
		if (!std::getline(file, line)) {
			spdlog::warn("Legacy ITEM csv is empty: {}", path.string());
			return out;
		}

		while (std::getline(file, line)) {
			const auto cols = SplitTab_(line);
			if (cols.size() < 43) {
				++out.invalid_row_count;
				continue;
			}

			ItemTemplate row{};
			row.item_id = ParseUInt_(cols[0]);
			if (row.item_id == 0) {
				++out.invalid_row_count;
				continue;
			}
			if (!seen_item_ids.insert(row.item_id).second) {
				++out.duplicate_item_id_count;
				continue;
			}

			row.equip_tribe = static_cast<int>(ParseUInt_(cols[7]));
			row.equip_part = static_cast<int>(ParseUInt_(cols[8]));
			row.vitality = static_cast<int>(ParseUInt_(cols[24]));
			row.ki = static_cast<int>(ParseUInt_(cols[25]));
			row.attack = static_cast<int>(ParseUInt_(cols[29]));
			row.defense = static_cast<int>(ParseUInt_(cols[30]));
			row.life = static_cast<int>(ParseUInt_(cols[41]));
			row.mana = static_cast<int>(ParseUInt_(cols[42]));
			out.templates.push_back(row);
		}

		out.loaded = !out.templates.empty();
		if (out.loaded) {
			spdlog::info(
				"Legacy ITEM csv parsed for bootstrap/import: {} count={} duplicates={} invalid_rows={}",
				path.string(),
				out.templates.size(),
				out.duplicate_item_id_count,
				out.invalid_row_count);
		}
		return out;
	}

	LegacyItemCsvLoadResult LoadLegacyItemTemplatesFromCsv()
	{
		return LoadLegacyItemTemplatesFromCsv(ResolveLegacyDataDir_() / "005_00002.csv");
	}

} // namespace svr
