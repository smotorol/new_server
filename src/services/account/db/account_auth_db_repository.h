#pragma once

#include "services/account/db/account_auth_job.h"

namespace dc::account {

    class AccountAuthDbRepository {
    public:
        template <typename DbConnT>
        static AccountAuthRequestResult ExecuteAuth(
            DbConnT& conn,
            const AccountAuthRequestJob& job);

    private:
        static bool VerifyPasswordHex_(
            std::string_view password,
            std::string_view password_hash_hex,
            std::string_view password_salt_hex);
    };

} // namespace dc::account
