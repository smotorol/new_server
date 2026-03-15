#include "core/crypto/sha256.h"

#include <array>
#include <cstring>

namespace dc::crypto {

    namespace {

        constexpr std::array<std::uint32_t, 64> kTable = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        inline std::uint32_t RotR(std::uint32_t x, std::uint32_t n)
        {
            return (x >> n) | (x << (32 - n));
        }

        inline std::uint32_t Ch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
        {
            return (x & y) ^ (~x & z);
        }

        inline std::uint32_t Maj(std::uint32_t x, std::uint32_t y, std::uint32_t z)
        {
            return (x & y) ^ (x & z) ^ (y & z);
        }

        inline std::uint32_t BigSigma0(std::uint32_t x)
        {
            return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22);
        }

        inline std::uint32_t BigSigma1(std::uint32_t x)
        {
            return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25);
        }

        inline std::uint32_t SmallSigma0(std::uint32_t x)
        {
            return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3);
        }

        inline std::uint32_t SmallSigma1(std::uint32_t x)
        {
            return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10);
        }

        std::vector<std::uint8_t> Sha256Bytes_(const std::uint8_t* data, std::size_t size)
        {
            std::uint32_t h0 = 0x6a09e667;
            std::uint32_t h1 = 0xbb67ae85;
            std::uint32_t h2 = 0x3c6ef372;
            std::uint32_t h3 = 0xa54ff53a;
            std::uint32_t h4 = 0x510e527f;
            std::uint32_t h5 = 0x9b05688c;
            std::uint32_t h6 = 0x1f83d9ab;
            std::uint32_t h7 = 0x5be0cd19;

            std::vector<std::uint8_t> msg(data, data + size);
            msg.push_back(0x80);

            while ((msg.size() % 64) != 56) {
                msg.push_back(0x00);
            }

            const std::uint64_t bit_len = static_cast<std::uint64_t>(size) * 8ULL;
            for (int i = 7; i >= 0; --i) {
                msg.push_back(static_cast<std::uint8_t>((bit_len >> (i * 8)) & 0xff));
            }

            for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
                std::uint32_t w[64]{};

                for (int i = 0; i < 16; ++i) {
                    const std::size_t j = chunk + (i * 4);
                    w[i] =
                        (static_cast<std::uint32_t>(msg[j]) << 24) |
                        (static_cast<std::uint32_t>(msg[j + 1]) << 16) |
                        (static_cast<std::uint32_t>(msg[j + 2]) << 8) |
                        (static_cast<std::uint32_t>(msg[j + 3]));
                }

                for (int i = 16; i < 64; ++i) {
                    w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
                }

                std::uint32_t a = h0;
                std::uint32_t b = h1;
                std::uint32_t c = h2;
                std::uint32_t d = h3;
                std::uint32_t e = h4;
                std::uint32_t f = h5;
                std::uint32_t g = h6;
                std::uint32_t h = h7;

                for (int i = 0; i < 64; ++i) {
                    const std::uint32_t t1 = h + BigSigma1(e) + Ch(e, f, g) + kTable[i] + w[i];
                    const std::uint32_t t2 = BigSigma0(a) + Maj(a, b, c);

                    h = g;
                    g = f;
                    f = e;
                    e = d + t1;
                    d = c;
                    c = b;
                    b = a;
                    a = t1 + t2;
                }

                h0 += a;
                h1 += b;
                h2 += c;
                h3 += d;
                h4 += e;
                h5 += f;
                h6 += g;
                h7 += h;
            }

            std::vector<std::uint8_t> out(32);
            const std::uint32_t hs[8] = { h0, h1, h2, h3, h4, h5, h6, h7 };
            for (int i = 0; i < 8; ++i) {
                out[i * 4 + 0] = static_cast<std::uint8_t>((hs[i] >> 24) & 0xff);
                out[i * 4 + 1] = static_cast<std::uint8_t>((hs[i] >> 16) & 0xff);
                out[i * 4 + 2] = static_cast<std::uint8_t>((hs[i] >> 8) & 0xff);
                out[i * 4 + 3] = static_cast<std::uint8_t>(hs[i] & 0xff);
            }
            return out;
        }

    } // namespace

    std::vector<std::uint8_t> Sha256(std::string_view text)
    {
        return Sha256Bytes_(
            reinterpret_cast<const std::uint8_t*>(text.data()),
            text.size());
    }

    std::vector<std::uint8_t> Sha256(const std::vector<std::uint8_t>& data)
    {
        if (data.empty()) {
            return Sha256Bytes_(nullptr, 0);
        }
        return Sha256Bytes_(data.data(), data.size());
    }

    std::string ToUpperHex(const std::vector<std::uint8_t>& bytes)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";

        std::string out;
        out.resize(bytes.size() * 2);

        for (std::size_t i = 0; i < bytes.size(); ++i) {
            out[i * 2 + 0] = kHex[(bytes[i] >> 4) & 0x0f];
            out[i * 2 + 1] = kHex[bytes[i] & 0x0f];
        }
        return out;
    }

} // namespace dc::crypto
