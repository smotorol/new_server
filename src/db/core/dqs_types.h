#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace svr::dqs {

	// 프로세스 코드(네 프로젝트의 eLine과 동일한 값 유지)
	enum class ProcessCode : std::uint8_t {
		world = 0,
		login = 1,
		control = 2,
		account = 3,

	};

	// DQS QueryCase (원본에서 실사용 2개만 우선)
	// NOTE: 실제 패킷에서 들어오는 값과 다르면 여기 숫자만 맞추면 됨.
	enum class QueryCase : std::uint8_t {
		login_world_character = 1,
		insert_log_data = 2,

		// 샘플: 월드 오픈(클라 요청) 처리
		open_world_notice = 100,

		// Redis write-behind: dirty set flush
		flush_dirty_chars = 110,
		flush_one_char = 111,
		world_character_enter_snapshot = 112,
		world_account_character_list = 113,

		account_auth = 120,
		account_character_list = 121,

	};

	enum class ResultCode : std::uint8_t {
		success = 0,
		db_error = 1,
		invalid_data = 2,
		conflict = 3,

	};

	// 슬롯풀용 고정 슬롯(원본 _DB_QRY_SYN_DATA 대체)
	struct DqsSlot final {
		static constexpr std::size_t max_data_size = 4096; // 운영하면서 조정

		std::uint8_t process_code = 0; // eLine 값 그대로
		std::uint8_t qry_case = 0; // QueryCase 값 그대로
		ResultCode result = ResultCode::success;

		bool in_use = false;
		bool done = false;

		std::uint16_t data_size = 0;
		std::array<std::uint8_t, max_data_size> data{};

		void reset() noexcept {
			process_code = 0;
			qry_case = 0;
			result = ResultCode::success;
			in_use = false;
			done = false;
			data_size = 0;

		}

	};

	// payload 맨 앞 4바이트를 world_code로 해석(원본 방식 호환)
	inline std::uint32_t read_world_code(const DqsSlot& slot) noexcept {
		std::uint32_t wc = 0;
		if (slot.data_size < sizeof(std::uint32_t)) return 0;
		std::memcpy(&wc, slot.data.data(), sizeof(std::uint32_t));
		return wc;

	}


} // namespace logsvr::dqs
