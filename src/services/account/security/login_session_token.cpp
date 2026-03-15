#include "services/account/security/login_session_token.h"

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace dc::account {

    namespace {
        constexpr char kHex[] = "0123456789ABCDEF";
    }

    std::string GenerateLoginSessionToken()
    {
        std::array<std::uint8_t, 32> bytes{};

        std::random_device rd;
        std::mt19937_64 gen(rd());

        for (auto& b : bytes) {
            b = static_cast<std::uint8_t>(gen() & 0xff);
        }

        std::string out;
        out.resize(bytes.size() * 2);

        for (std::size_t i = 0; i < bytes.size(); ++i) {
            out[i * 2 + 0] = kHex[(bytes[i] >> 4) & 0x0f];
            out[i * 2 + 1] = kHex[bytes[i] & 0x0f];
        }

        return out;
    }

} // namespace dc::account
