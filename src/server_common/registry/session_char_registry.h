#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace dc {

    class SessionCharRegistry {
    public:
        std::uint64_t FindCharIdBySession(std::uint32_t sid) const
        {
            std::lock_guard lk(mtx_);
            auto it = session_char_ids_.find(sid);
            if (it != session_char_ids_.end() && it->second != 0) {
                return it->second;
            }
            return 0;
        }

        void BindSessionCharId(std::uint32_t sid, std::uint64_t char_id)
        {
            std::lock_guard lk(mtx_);
            session_char_ids_[sid] = char_id;
        }

        std::uint64_t UnbindSessionCharId(std::uint32_t sid)
        {
            std::lock_guard lk(mtx_);
            auto it = session_char_ids_.find(sid);
            if (it == session_char_ids_.end()) {
                return 0;
            }

            const auto char_id = it->second;
            session_char_ids_.erase(it);
            return char_id;
        }

    private:
        mutable std::mutex mtx_;
        std::unordered_map<std::uint32_t, std::uint64_t> session_char_ids_;
    };

} // namespace dc
