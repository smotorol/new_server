#include "services/world/common/character_combat_stat_calculator.h"

#include <algorithm>

#include "services/world/common/item_stat_provider.h"
#include "services/world/common/legacy_level_csv_loader.h"

namespace svr {

	namespace {

		std::size_t ToLegacyTribeIndex_(std::uint16_t tribe) noexcept
		{
			return tribe <= 2 ? static_cast<std::size_t>(tribe) : 0u;
		}

		std::uint32_t JobAttackBonus_(std::uint16_t job) noexcept
		{
			switch (job) {
			case 1: return 4;
			case 2: return 2;
			case 3: return 1;
			default: return 0;
			}
		}

		std::uint32_t JobDefenseBonus_(std::uint16_t job) noexcept
		{
			switch (job) {
			case 1: return 1;
			case 2: return 2;
			case 3: return 4;
			default: return 0;
			}
		}

		std::uint32_t JobHpBonus_(std::uint16_t job) noexcept
		{
			switch (job) {
			case 1: return 20;
			case 2: return 10;
			case 3: return 15;
			default: return 0;
			}
		}

		std::uint32_t JobMpBonus_(std::uint16_t job) noexcept
		{
			switch (job) {
			case 1: return 5;
			case 2: return 20;
			case 3: return 10;
			default: return 0;
			}
		}

		std::uint32_t TribeAttackBonus_(std::uint16_t tribe) noexcept
		{
			return tribe == 0 ? 0u : 1u;
		}

		std::uint32_t LevelAttackBonus_(std::uint32_t level, std::uint16_t tribe) noexcept
		{
			const auto* row = GetLegacyLevelTable().Find(level);
			if (row != nullptr) {
				return row->attack[ToLegacyTribeIndex_(tribe)];
			}
			return level > 0 ? (level - 1u) : 0u;
		}

		std::uint32_t LevelDefenseBonus_(std::uint32_t level, std::uint16_t tribe) noexcept
		{
			const auto* row = GetLegacyLevelTable().Find(level);
			if (row != nullptr) {
				return row->defense[ToLegacyTribeIndex_(tribe)];
			}
			return level > 0 ? ((level - 1u) / 2u) : 0u;
		}

		std::uint32_t LevelLifeBonus_(std::uint32_t level, std::uint16_t tribe) noexcept
		{
			const auto* row = GetLegacyLevelTable().Find(level);
			if (row != nullptr) {
				return row->life[ToLegacyTribeIndex_(tribe)];
			}
			return level > 0 ? ((level - 1u) * 6u) : 0u;
		}

		std::uint32_t LevelManaBonus_(std::uint32_t level, std::uint16_t tribe) noexcept
		{
			const auto* row = GetLegacyLevelTable().Find(level);
			if (row != nullptr) {
				return row->mana[ToLegacyTribeIndex_(tribe)];
			}
			return level > 0 ? ((level - 1u) * 4u) : 0u;
		}

		std::uint32_t FallbackEquipAttackBonus_(const EquipSummary& equip) noexcept
		{
			return (equip.weapon_template_id != 0 ? 5u : 0u) +
				(equip.accessory_template_id != 0 ? 1u : 0u);
		}

		std::uint32_t FallbackEquipDefenseBonus_(const EquipSummary& equip) noexcept
		{
			return (equip.armor_template_id != 0 ? 4u : 0u) +
				(equip.costume_template_id != 0 ? 1u : 0u);
		}

		std::uint32_t FallbackEquipHpBonus_(const EquipSummary& equip) noexcept
		{
			return equip.armor_template_id != 0 ? 25u : 0u;
		}

		std::uint32_t FallbackEquipMpBonus_(const EquipSummary& equip) noexcept
		{
			return equip.accessory_template_id != 0 ? 15u : 0u;
		}

	} // namespace

	CharacterCombatRuntimeStats RecomputeCombatStats(const CharacterCoreState& core) noexcept
	{
		const auto safe_level = std::max<std::uint32_t>(1u, core.identity.level);
		const auto item_bonus = BuildItemStatBonus(core.equip);

		const auto equip_attack_bonus = item_bonus.matched_template ? item_bonus.attack : FallbackEquipAttackBonus_(core.equip);
		const auto equip_defense_bonus = item_bonus.matched_template ? item_bonus.defense : FallbackEquipDefenseBonus_(core.equip);
		const auto equip_hp_bonus = item_bonus.matched_template ? item_bonus.life : FallbackEquipHpBonus_(core.equip);
		const auto equip_mp_bonus = item_bonus.matched_template ? item_bonus.mana : FallbackEquipMpBonus_(core.equip);

		CharacterCombatRuntimeStats out{};
		out.max_hp =
			100u +
			LevelLifeBonus_(safe_level, core.identity.tribe) +
			JobHpBonus_(core.identity.job) +
			equip_hp_bonus;
		out.max_mp =
			100u +
			LevelManaBonus_(safe_level, core.identity.tribe) +
			JobMpBonus_(core.identity.job) +
			equip_mp_bonus;
		out.atk =
			20u +
			LevelAttackBonus_(safe_level, core.identity.tribe) +
			JobAttackBonus_(core.identity.job) +
			TribeAttackBonus_(core.identity.tribe) +
			equip_attack_bonus;
		out.def =
			3u +
			LevelDefenseBonus_(safe_level, core.identity.tribe) +
			JobDefenseBonus_(core.identity.job) +
			equip_defense_bonus;
		return out;
	}

} // namespace svr
