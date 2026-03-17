#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace svr::demo {

#pragma pack(push, 1)
	struct DemoCharState
	{
		std::uint64_t char_id = 0;
		std::uint32_t gold = 0;
		std::uint32_t version = 0;
	};
#pragma pack(pop)

	static_assert(sizeof(DemoCharState) == 16);

	inline std::string SerializeDemo(const DemoCharState& s)
	{
		return std::string(reinterpret_cast<const char*>(&s), sizeof(s));
	}

	inline bool TryDeserializeDemo(const std::string& blob, DemoCharState& out)
	{
		if (blob.size() != sizeof(DemoCharState)) {
			return false;
		}

		std::memcpy(&out, blob.data(), sizeof(out));
		return true;
	}

} // namespace svr::demo
