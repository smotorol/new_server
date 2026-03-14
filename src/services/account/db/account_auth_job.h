#pragma once

#include <cstdint>
#include <string>

namespace dc::account {

    struct AccountAuthRequestJob {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t request_id = 0;
        std::uint64_t selected_char_id = 0;

        std::string login_id;
        std::string password;
    };

    struct AccountAuthRequestResult {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t request_id = 0;

        bool ok = false;
        std::uint64_t account_id = 0;
        std::uint64_t char_id = 0;
        std::string fail_reason;
    };

} // namespace dc::account
