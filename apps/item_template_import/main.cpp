#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "db/odbc/odbc_wrapper.h"
#include "services/world/common/legacy_item_csv_loader.h"
#include "services/world/db/item_template_import_repository.h"

namespace {

	constexpr const char* kDefaultExtractItemCsv =
		"G:\\Programing\\Work\\new_server\\docs\\legacy_extract\\ts_zone_item_level\\original_item.csv";
	constexpr const char* kDefaultLegacyItemCsv =
		"G:\\Programing\\Work\\12sky1\\12sky1\\S07_TS_ZONE\\DATA\\005_00002.csv";
	constexpr const char* kDefaultSourceTag = "legacy_csv_import";

	std::filesystem::path ResolveDefaultSourcePath()
	{
		const auto extract = std::filesystem::path(kDefaultExtractItemCsv);
		if (std::filesystem::exists(extract)) {
			return extract;
		}
		return std::filesystem::path(kDefaultLegacyItemCsv);
	}

	void PrintUsage()
	{
		std::cout
			<< "Usage: item_template_import --conn <connection-string> [--source <csv-path>] [--source-tag <tag>] [--dry-run]\n"
			<< "Default source: " << ResolveDefaultSourcePath().string() << "\n";
	}

} // namespace

int main(int argc, char** argv)
{
	std::string conn_str;
	std::filesystem::path source_path = ResolveDefaultSourcePath();
	std::string source_tag = kDefaultSourceTag;
	bool dry_run = false;

	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		if (arg == "--conn" && i + 1 < argc) {
			conn_str = argv[++i];
		}
		else if (arg == "--source" && i + 1 < argc) {
			source_path = argv[++i];
		}
		else if (arg == "--source-tag" && i + 1 < argc) {
			source_tag = argv[++i];
		}
		else if (arg == "--dry-run") {
			dry_run = true;
		}
		else if (arg == "--help" || arg == "-h") {
			PrintUsage();
			return 0;
		}
	}

	if (conn_str.empty()) {
		PrintUsage();
		std::cerr << "Missing required --conn\n";
		return 2;
	}

	const auto load = svr::LoadLegacyItemTemplatesFromCsv(source_path);
	if (!load.loaded) {
		std::cerr << "Source load failed or empty: " << source_path.string() << "\n";
		return 3;
	}

	svr::ItemTemplateImportStats stats{};
	stats.invalid = load.invalid_row_count;
	stats.duplicates = load.duplicate_item_id_count;
	stats.source_rows = load.templates.size() + load.invalid_row_count + load.duplicate_item_id_count;

	try {
		db::OdbcConnection conn;
		conn.connect(conn_str);
		conn.execute("BEGIN TRANSACTION");

		for (const auto& item : load.templates) {
			svr::ItemTemplateImportRepository::UpsertItemTemplate(conn, item, source_tag, stats);
		}

		const auto total_rows = conn.execute_scalar_int("SELECT COUNT(1) FROM [NFX_GAME].[game].[item_template] WHERE [is_deleted] = 0");

		if (dry_run) {
			conn.execute("ROLLBACK TRANSACTION");
			spdlog::info("item_template import dry-run complete. source={} inserted={} updated={} skipped={} invalid={} duplicates={} live_rows={}",
				load.source_path.string(),
				stats.inserted,
				stats.updated,
				stats.skipped,
				stats.invalid,
				stats.duplicates,
				total_rows);
		}
		else {
			conn.execute("COMMIT TRANSACTION");
			spdlog::info("item_template import committed. source={} inserted={} updated={} skipped={} invalid={} duplicates={} live_rows={}",
				load.source_path.string(),
				stats.inserted,
				stats.updated,
				stats.skipped,
				stats.invalid,
				stats.duplicates,
				total_rows);
		}

		std::cout
			<< "source=" << load.source_path.string() << "\n"
			<< "dry_run=" << (dry_run ? 1 : 0) << "\n"
			<< "source_rows=" << stats.source_rows << "\n"
			<< "inserted=" << stats.inserted << "\n"
			<< "updated=" << stats.updated << "\n"
			<< "skipped=" << stats.skipped << "\n"
			<< "invalid=" << stats.invalid << "\n"
			<< "duplicates=" << stats.duplicates << "\n"
			<< "live_rows=" << total_rows << "\n";
	}
	catch (const std::exception& e) {
		std::cerr << "item_template import failed: " << e.what() << "\n";
		return 4;
	}

	return 0;
}
