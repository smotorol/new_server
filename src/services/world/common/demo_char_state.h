#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "services/world/common/character_core_state.h"

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

	inline svr::CharacterCoreState ToCharacterCoreState(
		const DemoCharState& s,
		std::uint64_t account_id = 0)
	{
		auto out = svr::MakeDefaultCharacterCoreState(account_id, s.char_id);
		out.hot.resources.gold = s.gold;
		out.hot.version = s.version;
		return out;
	}

	inline DemoCharState FromCharacterCoreState(const svr::CharacterCoreState& s) noexcept
	{
		DemoCharState out{};
		out.char_id = s.identity.char_id;
		out.gold = s.hot.resources.gold;
		out.version = s.hot.version;
		return out;
	}

	inline std::string SerializeDemo(const DemoCharState& s)
	{
		return std::string(reinterpret_cast<const char*>(&s), sizeof(s));
	}

	inline std::string SerializeDemo(const svr::CharacterCoreState& s)
	{
		return SerializeDemo(FromCharacterCoreState(s));
	}

	inline bool TryDeserializeDemo(const std::string& blob, DemoCharState& out)
	{
		if (blob.size() != sizeof(DemoCharState)) {
			return false;
		}

		std::memcpy(&out, blob.data(), sizeof(out));
		return true;
	}

	inline bool TryDeserializeDemo(
		const std::string& blob,
		svr::CharacterCoreState& out,
		std::uint64_t account_id = 0)
	{
		DemoCharState raw{};
		if (!TryDeserializeDemo(blob, raw)) {
			return false;
		}

		out = ToCharacterCoreState(raw, account_id);
		return true;
	}

} // namespace svr::demo
