#pragma once

#include <string_view>

#include "db/odbc/odbc_wrapper.h"
#include "services/account/db/account_auth_job.h"

namespace dc::account {

	class AccountAuthDbRepository {
	public:
		static AccountAuthRequestResult ExecuteAuth(
			db::OdbcConnection& conn,
			const AccountAuthRequestJob& job);

        static bool UpdateLastLoginAtUtc(
            db::OdbcConnection& conn,
            std::uint64_t account_id);
	private:
		static bool VerifyPasswordHex_(
			std::string_view password,
			std::string_view password_hash_hex,
			std::string_view password_salt_hex);
	};

} // namespace dc::account
