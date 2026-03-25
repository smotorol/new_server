#include "services/world/db/item_template_repository.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include "services/world/common/legacy_item_csv_loader.h"

namespace svr {

	namespace {

		struct RuntimeStatusState {
			std::mutex mtx;
			ItemTemplateRepositoryStatus status{};
		};

		RuntimeStatusState& GetStatusState_() noexcept
		{
			static RuntimeStatusState state;
			return state;
		}

		void UpdateStatus_(const ItemTemplateRepositoryLoadResult& result) noexcept
		{
			auto& state = GetStatusState_();
			std::lock_guard lk(state.mtx);
			state.status.loaded = result.loaded;
			state.status.ready = result.loaded;
			state.status.empty = (result.loaded_count == 0);
			state.status.preload_count = result.loaded_count;
			state.status.source = result.loaded ? result.source : "empty";
			if (result.fallback_used) {
				state.status.fallback_entered = true;
			}
			if (!result.note.empty()) {
				state.status.last_error_reason = result.note;
			}
		}

		ItemTemplateRepositoryLoadResult PublishTemplates_(
			std::vector<ItemTemplate> templates,
			std::size_t duplicates,
			std::size_t invalid_rows,
			bool fallback_used,
			std::string source,
			std::string note)
		{
			const bool has_templates = !templates.empty();
			auto& table = GetMutableCanonicalItemTemplateTable();
			table.Reset(has_templates, std::move(templates));

			ItemTemplateRepositoryLoadResult out{};
			out.loaded = table.loaded();
			out.fallback_used = fallback_used;
			out.loaded_count = table.size();
			out.duplicate_item_id_count = duplicates;
			out.invalid_row_count = invalid_rows;
			out.source = std::move(source);
			out.note = std::move(note);
			UpdateStatus_(out);
			return out;
		}

		void MarkError_(std::string reason, std::string source) noexcept
		{
			auto& state = GetStatusState_();
			std::lock_guard lk(state.mtx);
			state.status.loaded = false;
			state.status.ready = false;
			state.status.empty = true;
			state.status.preload_count = 0;
			state.status.source = std::move(source);
			state.status.last_error_reason = std::move(reason);
		}

	} // namespace

	ItemTemplateRepositoryLoadResult ItemTemplateRepository::LoadCanonicalTableFromDb(db::OdbcConnection& conn)
	{
		std::vector<ItemTemplate> templates;
		std::unordered_set<std::uint32_t> seen_item_ids;
		std::size_t duplicates = 0;
		std::size_t invalid_rows = 0;

		try {
			db::OdbcStatement stmt(conn);
			stmt.prepare(
				"SELECT [item_id], [equip_part], [equip_tribe], [attack], [defense], [life], [mana], [vitality], [ki] "
				"FROM [NFX_GAME].[game].[item_template] "
				"WHERE [is_deleted] = 0 "
				"ORDER BY [item_id] ASC");
			stmt.execute();

			while (stmt.fetch()) {
				ItemTemplate row{};
				row.item_id = static_cast<std::uint32_t>(stmt.get_int(1));
				if (row.item_id == 0) {
					++invalid_rows;
					continue;
				}
				if (!seen_item_ids.insert(row.item_id).second) {
					++duplicates;
					continue;
				}
				row.equip_part = stmt.get_int(2);
				row.equip_tribe = stmt.get_int(3);
				row.attack = stmt.get_int(4);
				row.defense = stmt.get_int(5);
				row.life = stmt.get_int(6);
				row.mana = stmt.get_int(7);
				row.vitality = stmt.get_int(8);
				row.ki = stmt.get_int(9);
				templates.push_back(row);
			}
		}
		catch (const std::exception& e) {
			spdlog::warn("ItemTemplateRepository DB load failed: {}", e.what());
			MarkError_("db_load_failed", "db");
			return PublishTemplates_({}, 0, 0, false, "db", "db_load_failed");
		}

		auto result = PublishTemplates_(
			std::move(templates),
			duplicates,
			invalid_rows,
			false,
			"db",
			"loaded_from_game.item_template");

		if (result.loaded) {
			spdlog::info(
				"ItemTemplateRepository loaded from DB. count={} duplicates={} invalid_rows={}",
				result.loaded_count,
				result.duplicate_item_id_count,
				result.invalid_row_count);
		}
		else {
			spdlog::warn("ItemTemplateRepository DB source is empty. fallback may be required.");
		}
		return result;
	}

	ItemTemplateRepositoryLoadResult ItemTemplateRepository::BootstrapCanonicalTableFromLegacyCsv()
	{
		auto load = LoadLegacyItemTemplatesFromCsv();
		auto result = PublishTemplates_(
			std::move(load.templates),
			load.duplicate_item_id_count,
			load.invalid_row_count,
			true,
			"legacy_csv",
			"bootstrap_from_legacy_item_csv");

		if (result.loaded) {
			spdlog::warn(
				"ItemTemplateRepository fallback bootstrap activated. source=legacy_csv count={} duplicates={} invalid_rows={}",
				result.loaded_count,
				result.duplicate_item_id_count,
				result.invalid_row_count);
		}
		else {
			spdlog::warn("ItemTemplateRepository fallback bootstrap also produced no templates.");
		}
		return result;
	}

	const ItemTemplate* ItemTemplateRepository::Find(std::uint32_t item_id) noexcept
	{
		return GetCanonicalItemTemplateTable().Find(item_id);
	}

	bool ItemTemplateRepository::Empty() noexcept
	{
		return GetCanonicalItemTemplateTable().empty();
	}

	bool ItemTemplateRepository::IsLoaded() noexcept
	{
		return GetCanonicalItemTemplateTable().loaded();
	}

	void ItemTemplateRepository::SetLegacyFallbackAllowed(bool allowed) noexcept
	{
		auto& state = GetStatusState_();
		std::lock_guard lk(state.mtx);
		state.status.fallback_allowed = allowed;
	}

	bool ItemTemplateRepository::LegacyFallbackAllowed() noexcept
	{
		auto& state = GetStatusState_();
		std::lock_guard lk(state.mtx);
		return state.status.fallback_allowed;
	}

	void ItemTemplateRepository::RecordTemplateMiss(std::uint32_t item_id) noexcept
	{
		auto& state = GetStatusState_();
		std::lock_guard lk(state.mtx);
		++state.status.miss_count;
		if (item_id != 0 && state.status.miss_samples.size() < 8) {
			const auto exists = std::find(state.status.miss_samples.begin(), state.status.miss_samples.end(), item_id);
			if (exists == state.status.miss_samples.end()) {
				state.status.miss_samples.push_back(item_id);
			}
		}
	}

	ItemTemplateRepositoryStatus ItemTemplateRepository::SnapshotStatus()
	{
		auto& state = GetStatusState_();
		std::lock_guard lk(state.mtx);
		return state.status;
	}

} // namespace svr


