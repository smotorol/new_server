#pragma once

#include <cstddef>
#include <cstdint>

namespace svr {

	enum class WorldLineId : std::uint32_t
	{
		World = 0,
		Control,
		Zone,
		Count
	};

	inline constexpr std::size_t kWorldLineCount =
		static_cast<std::size_t>(WorldLineId::Count);

} // namespace svr
