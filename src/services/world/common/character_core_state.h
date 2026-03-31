#pragma once

#include "services/world/common/character_identity.h"
#include "services/world/common/character_combat_runtime_stats.h"
#include "services/world/common/character_presentation.h"
#include "services/world/common/character_runtime_hot_state.h"

namespace svr {

	struct CharacterCoreState
	{
		CharacterIdentity identity{};
		AppearanceSummary appearance{};
		EquipSummary equip{};
		CharacterCombatRuntimeStats combat_stats{};
		CharacterRuntimeHotState hot{};
	};

	inline CharacterCoreState MakeDefaultCharacterCoreState(
		std::uint64_t account_id,
		std::uint64_t char_id) noexcept
	{
		CharacterCoreState state{};
		state.identity.account_id = account_id;
		state.identity.char_id = char_id;
		state.combat_stats = MakeDefaultCharacterCombatRuntimeStats();
		state.hot.resources.max_hp = state.combat_stats.max_hp;
		state.hot.resources.max_mp = state.combat_stats.max_mp;
		state.hot.resources.current_hp = state.combat_stats.max_hp;
		state.hot.resources.current_mp = state.combat_stats.max_mp;
		state.hot.resources.gold = 1000;
		state.hot.position.map_id = 1;
		state.hot.position.zone_id = 1;
		return state;
	}

} // namespace svr
