#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace svr {

	struct LegacyLevelEntry
	{
		bool valid = false;
		std::array<std::uint32_t, 3> attack{};
		std::array<std::uint32_t, 3> defense{};
		std::array<std::uint32_t, 3> life{};
		std::array<std::uint32_t, 3> mana{};
	};

	class LegacyLevelTable final
	{
	public:
		[[nodiscard]] bool loaded() const noexcept { return loaded_; }
		[[nodiscard]] const LegacyLevelEntry* Find(std::uint32_t level) const noexcept;
		void Reset(bool loaded, std::vector<LegacyLevelEntry> entries) noexcept;

	private:
		bool loaded_ = false;
		std::vector<LegacyLevelEntry> entries_{};
	};

	const LegacyLevelTable& GetLegacyLevelTable();

} // namespace svr
