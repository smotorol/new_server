#include "services/world/common/legacy_level_csv_loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
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

		LegacyLevelTable LoadLegacyLevelTable_()
		{
			LegacyLevelTable table{};
			std::vector<LegacyLevelEntry> entries(146);

			const auto path = ResolveLegacyDataDir_() / "005_00001.csv";
			std::ifstream file(path);
			if (!file.is_open()) {
				spdlog::warn("Legacy LEVEL csv not found: {}. RecomputeCombatStats will use fallback level scaling.", path.string());
				table.Reset(false, {});
				return table;
			}

			std::string line;
			if (!std::getline(file, line)) {
				spdlog::warn("Legacy LEVEL csv is empty: {}", path.string());
				table.Reset(false, {});
				return table;
			}

			while (std::getline(file, line)) {
				const auto cols = SplitTab_(line);
				if (cols.size() < 28) {
					continue;
				}

				const auto level = ParseUInt_(cols[0]);
				if (level == 0 || level >= entries.size()) {
					continue;
				}

				auto& row = entries[level];
				row.valid = true;
				for (std::size_t i = 0; i < 3; ++i) {
					row.attack[i] = ParseUInt_(cols[6 + i]);
					row.defense[i] = ParseUInt_(cols[9 + i]);
					row.life[i] = ParseUInt_(cols[21 + i]);
					row.mana[i] = ParseUInt_(cols[24 + i]);
				}
			}

			spdlog::info("Legacy LEVEL csv loaded for stat mapping: {}", path.string());
			table.Reset(true, std::move(entries));
			return table;
		}

	} // namespace

	const LegacyLevelEntry* LegacyLevelTable::Find(std::uint32_t level) const noexcept
	{
		if (!loaded_ || level == 0 || level >= entries_.size()) {
			return nullptr;
		}
		if (!entries_[level].valid) {
			return nullptr;
		}
		return &entries_[level];
	}

	void LegacyLevelTable::Reset(bool loaded, std::vector<LegacyLevelEntry> entries) noexcept
	{
		loaded_ = loaded;
		entries_ = std::move(entries);
	}

	const LegacyLevelTable& GetLegacyLevelTable()
	{
		static std::once_flag once;
		static LegacyLevelTable table{};
		std::call_once(once, [] {
			table = LoadLegacyLevelTable_();
		});
		return table;
	}

} // namespace svr
