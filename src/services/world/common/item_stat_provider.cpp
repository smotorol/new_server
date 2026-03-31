#include "services/world/common/item_stat_provider.h"

#include <mutex>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "services/world/db/item_template_repository.h"

namespace svr {

	namespace {

		void LogTemplateMissOnce_(std::uint32_t item_id) noexcept
		{
			static std::mutex mtx;
			static std::unordered_set<std::uint32_t> logged_ids;
			if (item_id == 0) {
				return;
			}
			std::lock_guard lk(mtx);
			if (!logged_ids.insert(item_id).second) {
				return;
			}
			spdlog::warn("ItemStatProvider template miss. item_id={} source=item_template_repository", item_id);
		}

		void LogRepositoryEmptyOnce_() noexcept
		{
			static std::once_flag once;
			std::call_once(once, [] {
				spdlog::warn("ItemStatProvider is using an empty canonical repository. fallback bonuses may be applied.");
			});
		}

		void AccumulateFromTemplate_(ItemStatBonus& out, std::uint32_t item_id) noexcept
		{
			if (item_id == 0) {
				return;
			}
			const auto* templ = ItemTemplateRepository::Find(item_id);
			if (templ == nullptr) {
				ItemTemplateRepository::RecordTemplateMiss(item_id);
				LogTemplateMissOnce_(item_id);
				return;
			}
			out.matched_template = true;
			out.attack += static_cast<std::uint32_t>(templ->attack > 0 ? templ->attack : 0);
			out.defense += static_cast<std::uint32_t>(templ->defense > 0 ? templ->defense : 0);
			out.life += static_cast<std::uint32_t>(templ->life > 0 ? templ->life : 0);
			out.mana += static_cast<std::uint32_t>(templ->mana > 0 ? templ->mana : 0);
		}

	} // namespace

	ItemStatBonus BuildItemStatBonus(const EquipSummary& equip) noexcept
	{
		if (!ItemTemplateRepository::IsLoaded()) {
			LogRepositoryEmptyOnce_();
		}

		ItemStatBonus out{};
		AccumulateFromTemplate_(out, equip.weapon_template_id);
		AccumulateFromTemplate_(out, equip.armor_template_id);
		AccumulateFromTemplate_(out, equip.costume_template_id);
		AccumulateFromTemplate_(out, equip.accessory_template_id);
		return out;
	}

} // namespace svr