#pragma once

#include <cstdint>

namespace svr {

	struct CharacterCombatRuntimeStats
	{
		std::uint32_t max_hp = 100;
		std::uint32_t max_mp = 100;
		std::uint32_t atk = 20;
		std::uint32_t def = 3;
	};

	inline CharacterCombatRuntimeStats MakeDefaultCharacterCombatRuntimeStats() noexcept
	{
		return {};
	}

} // namespace svr
