#pragma once

#include <cstdint>

namespace svr {

	struct CharacterPosition
	{
		std::uint32_t map_id = 0;
		std::uint32_t zone_id = 0;
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::int32_t z = 0;
		std::int16_t heading = 0;
	};

	struct CharacterResourceState
	{
		std::uint32_t current_hp = 100;
		// Compatibility mirror only. Authoritative max values live in CharacterCombatRuntimeStats.
		std::uint32_t max_hp = 100;
		std::uint32_t current_mp = 100;
		// Compatibility mirror only. Authoritative max values live in CharacterCombatRuntimeStats.
		std::uint32_t max_mp = 100;
		std::uint32_t gold = 0;
	};

	enum class CharacterDirtyFlags : std::uint32_t
	{
		none = 0,
		resources = 1u << 0,
		position = 1u << 1,
		presentation = 1u << 2,
		progression = 1u << 3,
	};

	inline constexpr CharacterDirtyFlags operator|(
		CharacterDirtyFlags lhs,
		CharacterDirtyFlags rhs) noexcept
	{
		return static_cast<CharacterDirtyFlags>(
			static_cast<std::uint32_t>(lhs) |
			static_cast<std::uint32_t>(rhs));
	}

	inline constexpr CharacterDirtyFlags& operator|=(
		CharacterDirtyFlags& lhs,
		CharacterDirtyFlags rhs) noexcept
	{
		lhs = lhs | rhs;
		return lhs;
	}

	struct CharacterRuntimeHotState
	{
		CharacterResourceState resources{};
		CharacterPosition position{};
		bool alive = true;
		bool in_world = false;
		std::uint32_t version = 0;
		CharacterDirtyFlags dirty_flags = CharacterDirtyFlags::none;
	};

} // namespace svr
