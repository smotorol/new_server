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

	[[nodiscard]] inline constexpr std::uint64_t PackSessionKey(
		std::uint32_t sid,
		std::uint32_t serial) noexcept
	{
		return (static_cast<std::uint64_t>(sid) << 32) |
			static_cast<std::uint64_t>(serial);
	}

	[[nodiscard]] inline constexpr std::uint32_t UnpackSessionSid(
		std::uint64_t session_key) noexcept
	{
		return static_cast<std::uint32_t>(session_key >> 32);
	}

	[[nodiscard]] inline constexpr std::uint32_t UnpackSessionSerial(
		std::uint64_t session_key) noexcept
	{
		return static_cast<std::uint32_t>(session_key & 0xFFFFFFFFULL);
	}

	[[nodiscard]] inline constexpr bool MatchesPackedSessionKey(
		std::uint64_t packed_key,
		std::uint32_t sid,
		std::uint32_t serial) noexcept
	{
		return packed_key == PackSessionKey(sid, serial);
	}

} // namespace dc
