#pragma once

#include <cstdint>

namespace proto {

    enum class WorldC2SMsg : std::uint16_t
    {
        enter_world_with_token = 2001,
    };

    enum class WorldS2CMsg : std::uint16_t
    {
        enter_world_result = 2101,
        kick_notify = 2102,
    };

    enum class WorldKickReason : std::uint16_t
    {
        unknown = 0,
        duplicate_login = 1,
        server_shutdown = 2,
        admin_kick = 3,
    };

#pragma pack(push, 1)
    struct C2S_enter_world_with_token
    {
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        char world_token[33]{};
    };

    struct S2C_enter_world_result
    {
        std::uint8_t ok = 0;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
    };

    struct S2C_kick_notify
    {
        std::uint16_t reason = static_cast<std::uint16_t>(WorldKickReason::unknown);
        std::uint64_t char_id = 0;
    };
#pragma pack(pop)

} // namespace proto
