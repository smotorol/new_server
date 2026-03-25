#pragma once

#include "services/world/common/character_core_state.h"

namespace svr {

	CharacterCombatRuntimeStats RecomputeCombatStats(const CharacterCoreState& core) noexcept;

} // namespace svr
