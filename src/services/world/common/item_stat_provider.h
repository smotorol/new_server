#pragma once

#include <cstdint>

#include "services/world/common/character_presentation.h"

namespace svr {

	struct ItemStatBonus
	{
		bool matched_template = false;
		std::uint32_t attack = 0;
		std::uint32_t defense = 0;
		std::uint32_t life = 0;
		std::uint32_t mana = 0;
	};

	ItemStatBonus BuildItemStatBonus(const EquipSummary& equip) noexcept;

} // namespace svr
