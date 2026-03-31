#pragma once

#include <cstdint>
#include <string>

namespace svr {

	struct CharacterIdentity
	{
		std::uint64_t char_id = 0;
		std::uint64_t account_id = 0;
		std::string char_name;
		std::uint32_t level = 1;
		std::uint16_t job = 0;
		std::uint16_t tribe = 0;

		[[nodiscard]] bool valid() const noexcept
		{
			return char_id != 0 && account_id != 0;
		}
	};

} // namespace svr
