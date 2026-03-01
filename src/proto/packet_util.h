#pragma once
#include "../net/msg_header.h"
#include <cstdint>
#include <cstring>

namespace proto {

    inline std::uint16_t get_type_u16(const _MSG_HEADER& h) {
        return (std::uint16_t)h.m_byType[0] | ((std::uint16_t)h.m_byType[1] << 8);
    }

    inline void set_type_u16(_MSG_HEADER& h, std::uint16_t t) {
        h.m_byType[0] = (std::uint8_t)(t & 0xFF);
        h.m_byType[1] = (std::uint8_t)((t >> 8) & 0xFF);
    }

    inline _MSG_HEADER make_header(std::uint16_t type, std::uint16_t body_size) {
        _MSG_HEADER h{};
        h.m_wSize = (std::uint16_t)(MSG_HEADER_SIZE + body_size);
        set_type_u16(h, type);
        return h;
    }

    template <class T>
    inline const T* as(const char* body, std::size_t len) {
        if (!body || len < sizeof(T)) return nullptr;
        return reinterpret_cast<const T*>(body);
    }

} // namespace proto
