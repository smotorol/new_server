#pragma once

#include <cstdint>

namespace dc {

	[[nodiscard]] inline constexpr bool IsValidSessionKey(
		std::uint32_t sid,
		std::uint32_t serial) noexcept
	{
		return sid != 0 && serial != 0;
	}

	[[nodiscard]] inline constexpr bool IsSameSessionKey(
		std::uint32_t lhs_sid,
		std::uint32_t lhs_serial,
		std::uint32_t rhs_sid,
		std::uint32_t rhs_serial) noexcept
	{
		return lhs_sid == rhs_sid && lhs_serial == rhs_serial;
	}

} // namespace dc
