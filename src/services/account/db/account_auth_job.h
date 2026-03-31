#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "db/core/dqs_payloads.h"
#include "db/core/dqs_results.h"
#include "services/account/security/login_session_token.h"

namespace dc::account {

    struct AccountAuthRequestJob {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t request_id = 0;

        std::string login_id;
        std::string password;
    };

    struct AccountAuthRequestResult {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t request_id = 0;

        bool ok = false;
        std::uint64_t account_id = 0;
        bool should_update_last_login_at_utc = false;
        std::string login_session;
        std::string fail_reason;
    };

    struct AccountCharacterListRequestJob {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
    };

    struct AccountCharacterSummary {
        std::uint64_t char_id = 0;
        std::string char_name;
        std::uint32_t level = 0;
        std::uint16_t job = 0;
        std::uint32_t appearance_code = 0;
        std::uint64_t last_login_at_epoch_sec = 0;
    };

    struct AccountCharacterListRequestResult {
        std::uint32_t sid = 0;
        std::uint32_t serial = 0;
        std::uint64_t trace_id = 0;
        std::uint64_t request_id = 0;
        std::uint64_t account_id = 0;
        bool ok = false;
        std::vector<AccountCharacterSummary> characters;
        std::string fail_reason;
    };

    inline AccountAuthRequestJob MakeAccountAuthRequestJob(
        const svr::dqs_payload::AccountAuthRequest& payload)
    {
        AccountAuthRequestJob job{};
        job.sid = payload.sid;
        job.serial = payload.serial;
        job.request_id = payload.request_id;
        job.login_id = payload.login_id;
        job.password = payload.password;
        return job;
    }

    inline svr::dqs_result::AccountAuthResult MakePendingAccountAuthResult(
        const svr::dqs_payload::AccountAuthRequest& payload)
    {
        svr::dqs_result::AccountAuthResult rr{};
        rr.trace_id = payload.trace_id;
        rr.sid = payload.sid;
        rr.serial = payload.serial;
        rr.request_id = payload.request_id;
        rr.ok = 0;
        rr.account_id = 0;
        rr.login_session[0] = '\0';
        rr.result = svr::dqs::ResultCode::success;
        rr.fail_reason[0] = '\0';
        return rr;
    }

    inline void ApplyAccountAuthRequestResult(
        const AccountAuthRequestResult& db_result,
        svr::dqs_result::AccountAuthResult& rr)
    {
        rr.ok = db_result.ok ? 1 : 0;
        rr.account_id = db_result.account_id;

		if (db_result.ok) {
			const auto login_session = dc::account::GenerateLoginSessionToken();
			std::snprintf(
				rr.login_session,
				sizeof(rr.login_session),
				"%s",
				login_session.c_str());
		}

		std::snprintf(
            rr.fail_reason,
            sizeof(rr.fail_reason),
            "%.*s",
            static_cast<int>(db_result.fail_reason.size()),
            db_result.fail_reason.c_str());
    }

    inline void SetAccountAuthDbError(
        svr::dqs_result::AccountAuthResult& rr,
        std::string_view reason)
    {
        rr.result = svr::dqs::ResultCode::db_error;
        std::snprintf(
            rr.fail_reason,
            sizeof(rr.fail_reason),
            "%.*s",
            static_cast<int>(reason.size()),
            reason.data());
    }

    inline AccountCharacterListRequestJob MakeAccountCharacterListRequestJob(
        const svr::dqs_payload::AccountCharacterListRequest& payload)
    {
        AccountCharacterListRequestJob job{};
        job.sid = payload.sid;
        job.serial = payload.serial;
        job.trace_id = payload.trace_id;
        job.request_id = payload.request_id;
        job.account_id = payload.account_id;
        return job;
    }

    inline svr::dqs_result::AccountCharacterListResult MakePendingAccountCharacterListResult(
        const svr::dqs_payload::AccountCharacterListRequest& payload)
    {
        svr::dqs_result::AccountCharacterListResult rr{};
        rr.trace_id = payload.trace_id;
        rr.sid = payload.sid;
        rr.serial = payload.serial;
        rr.request_id = payload.request_id;
        rr.account_id = payload.account_id;
        rr.ok = 0;
        rr.count = 0;
        rr.fail_reason[0] = '\0';
        rr.result = svr::dqs::ResultCode::success;
        return rr;
    }

    inline void ApplyAccountCharacterListRequestResult(
        const AccountCharacterListRequestResult& db_result,
        svr::dqs_result::AccountCharacterListResult& rr)
    {
        rr.ok = db_result.ok ? 1 : 0;
        rr.count = static_cast<std::uint16_t>(
            std::min<std::size_t>(db_result.characters.size(), dc::k_character_list_max_count));
        for (std::size_t i = 0; i < rr.count; ++i) {
            const auto& src = db_result.characters[i];
            auto& dst = rr.characters[i];
            dst.char_id = src.char_id;
            std::snprintf(dst.char_name, sizeof(dst.char_name), "%.*s",
                static_cast<int>(src.char_name.size()), src.char_name.c_str());
            dst.level = src.level;
            dst.job = src.job;
            dst.appearance_code = src.appearance_code;
            dst.last_login_at_epoch_sec = src.last_login_at_epoch_sec;
        }
        std::snprintf(
            rr.fail_reason,
            sizeof(rr.fail_reason),
            "%.*s",
            static_cast<int>(db_result.fail_reason.size()),
            db_result.fail_reason.c_str());
    }

    inline void SetAccountCharacterListDbError(
        svr::dqs_result::AccountCharacterListResult& rr,
        std::string_view reason)
    {
        rr.result = svr::dqs::ResultCode::db_error;
        std::snprintf(
            rr.fail_reason,
            sizeof(rr.fail_reason),
            "%.*s",
            static_cast<int>(reason.size()),
            reason.data());
    }

} // namespace dc::account
