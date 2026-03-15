#pragma once

#include <string>

namespace dc::account {

	// 32바이트 랜덤값을 64자리 대문자 hex 문자열로 만든다.
	std::string GenerateLoginSessionToken();

} // namespace dc::account
