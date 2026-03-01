#pragma once
#include <nanodbc/nanodbc.h>

namespace db {

    template <typename T>
    T execute_scalar(nanodbc::connection& conn, const std::string& sql)
    {
        nanodbc::result r = nanodbc::execute(conn, sql);
        if (!r.next())
            throw std::runtime_error("execute_scalar: no result");

        return r.get<T>(0);
    }

	// 캐릭터 상태 저장 샘플 (Redis write-behind의 최종 목적지)
		// NOTE: 실제 테이블/프로시저에 맞게 SQL을 교체하세요.
		//  - 예시: EXEC dbo.usp_SaveCharacterState @CharId=?, @Blob=?
	inline void save_character_blob(nanodbc::connection& conn, std::uint64_t char_id, const std::string& blob)
	{
		(void)blob;
		// TODO: 실제 스키마에 맞게 구현
			// nanodbc::statement stmt(conn);
			// prepare(stmt, "UPDATE Character SET Blob=? WHERE CharId=?");
			// stmt.bind(0, blob.data(), (int)blob.size());
			// stmt.bind(1, (long long)char_id);
			// execute(stmt);

			// 샘플: 연결 확인만 수행
		(void)execute_scalar<int>(conn, "SELECT 1");
	}

}
