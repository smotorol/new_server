#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dc {

	struct LineDescriptor
	{
		std::uint32_t id = 0;
		std::string name;
		std::uint16_t port = 0;
		bool verbose_session_log = true;
		std::size_t max_sessions = 0; // 0 = unlimited
	};

} // namespace dc
