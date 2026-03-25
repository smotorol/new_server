#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace dc {

    struct LoginSessionAuthState
    {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t trace_id = 0;

        bool logged_in = false;

        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::uint16_t selected_world_id = 0;
        std::uint16_t selected_channel_id = 0;
        std::uint32_t selected_world_server_id = 0;
        std::string world_host;
        std::uint16_t world_port = 0;

        std::string login_session;
        std::string world_token;
        std::chrono::steady_clock::time_point issued_at{};
        std::chrono::steady_clock::time_point expires_at{};
    };

} // namespace dc
