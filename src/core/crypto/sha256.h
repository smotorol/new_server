#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dc::crypto {

    // 입력 바이트 배열의 SHA-256 digest를 계산한다.
    std::vector<std::uint8_t> Sha256(std::string_view text);
    std::vector<std::uint8_t> Sha256(const std::vector<std::uint8_t>& data);

    // digest를 대문자 HEX 문자열로 변환한다.
    std::string ToUpperHex(const std::vector<std::uint8_t>& bytes);

} // namespace dc::crypto
