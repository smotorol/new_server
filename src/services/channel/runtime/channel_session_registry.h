#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace svr {

	class ChannelSessionRegistry {
	public:
		std::uint64_t FindCharIdBySession(std::uint32_t sid) const;
		void BindSessionCharId(std::uint32_t sid, std::uint64_t char_id);
		std::uint64_t UnbindSessionCharId(std::uint32_t sid);

	private:
		mutable std::mutex mtx_;
		std::unordered_map<std::uint32_t, std::uint64_t> session_char_ids_;
	};

} // namespace svr
