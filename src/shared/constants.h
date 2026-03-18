#pragma once
#include <cstddef>

namespace dc {
	inline constexpr std::size_t k_login_session_max_len = 64;
	inline constexpr std::size_t k_world_token_max_len = 32;
	inline constexpr std::size_t k_login_id_max_len = 32;
	inline constexpr std::size_t k_login_pw_max_len = 64;
	inline constexpr std::size_t k_world_host_max_len = 64;
	inline constexpr std::size_t k_auth_fail_reason_max_len = 64;
	inline constexpr std::size_t k_max_world_name_len = 32;
	inline constexpr std::size_t k_service_name_max_len = 32;
};
