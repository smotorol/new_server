#pragma once
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)
struct _MSG_HEADER
{
    std::uint16_t m_wSize;    // 전체 메시지 크기(헤더 포함)
    std::uint8_t  m_byType[2];// 타입(2바이트)
};
#pragma pack(pop)

static_assert(sizeof(_MSG_HEADER) == 4, "_MSG_HEADER must be 4 bytes");
constexpr std::size_t MSG_HEADER_SIZE = sizeof(_MSG_HEADER);
