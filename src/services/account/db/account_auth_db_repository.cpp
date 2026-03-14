#include "services/account/db/account_auth_db_repository.h"

#include <optional>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

// 네 현재 AccountLineRuntime 코드가 이 헤더를 쓰고 있으므로 그대로 맞춤
#include "db/odbc/statement.h"

namespace dc::account {

    namespace {

        struct AccountRow final {
            std::uint64_t account_id = 0;
            std::string login_id;
            std::uint8_t account_state = 0;
            std::string password_hash_hex;
            std::string password_salt_hex;
        };

        struct CharacterRow final {
            std::uint64_t char_id = 0;
            std::uint64_t account_id = 0;
        };

        template <typename DbConnT>
        std::optional<AccountRow> QueryAccountRow_(
            DbConnT& conn,
            std::string_view login_id)
        {
            dc::db::OdbcStatement stmt(conn);

            if (!stmt.Prepare(
                "SELECT TOP (1) "
                "    [account_id], "
                "    [login_id], "
                "    [account_state], "
                "    CONVERT(VARCHAR(512), [password_hash], 2) AS [password_hash_hex], "
                "    CONVERT(VARCHAR(256), [password_salt], 2) AS [password_salt_hex] "
                "FROM [auth].[account] "
                "WHERE [login_id] = ? "
                "  AND [is_deleted] = 0"))
            {
                spdlog::error("QueryAccountRow_ prepare failed.");
                return std::nullopt;
            }

            if (!stmt.BindInputString(1, std::string(login_id))) {
                spdlog::error("QueryAccountRow_ bind failed.");
                return std::nullopt;
            }

            if (!stmt.Execute()) {
                spdlog::error("QueryAccountRow_ execute failed. login_id={}", login_id);
                return std::nullopt;
            }

            if (!stmt.Fetch()) {
                return std::nullopt;
            }

            AccountRow row{};
            row.account_id = stmt.GetUInt64(1);
            row.login_id = stmt.GetString(2);
            row.account_state = static_cast<std::uint8_t>(stmt.GetInt(3));
            row.password_hash_hex = stmt.GetString(4);
            row.password_salt_hex = stmt.GetString(5);
            return row;
        }

        template <typename DbConnT>
        std::optional<CharacterRow> QueryCharacterById_(
            DbConnT& conn,
            std::uint64_t char_id)
        {
            dc::db::OdbcStatement stmt(conn);

            if (!stmt.Prepare(
                "SELECT TOP (1) [char_id], [account_id] "
                "FROM [NFX_GAME].[game].[character] "
                "WHERE [char_id] = ? "
                "  AND [char_state] = 1"))
            {
                spdlog::error("QueryCharacterById_ prepare failed.");
                return std::nullopt;
            }

            if (!stmt.BindInputUInt64(1, char_id)) {
                spdlog::error("QueryCharacterById_ bind failed.");
                return std::nullopt;
            }

            if (!stmt.Execute()) {
                spdlog::error("QueryCharacterById_ execute failed. char_id={}", char_id);
                return std::nullopt;
            }

            if (!stmt.Fetch()) {
                return std::nullopt;
            }

            CharacterRow row{};
            row.char_id = stmt.GetUInt64(1);
            row.account_id = stmt.GetUInt64(2);
            return row;
        }

        template <typename DbConnT>
        std::optional<CharacterRow> QueryDefaultCharacterByAccountId_(
            DbConnT& conn,
            std::uint64_t account_id)
        {
            dc::db::OdbcStatement stmt(conn);

            if (!stmt.Prepare(
                "SELECT TOP (1) [char_id], [account_id] "
                "FROM [NFX_GAME].[game].[character] "
                "WHERE [account_id] = ? "
                "  AND [char_state] = 1 "
                "ORDER BY [char_id] ASC"))
            {
                spdlog::error("QueryDefaultCharacterByAccountId_ prepare failed.");
                return std::nullopt;
            }

            if (!stmt.BindInputUInt64(1, account_id)) {
                spdlog::error("QueryDefaultCharacterByAccountId_ bind failed.");
                return std::nullopt;
            }

            if (!stmt.Execute()) {
                spdlog::error(
                    "QueryDefaultCharacterByAccountId_ execute failed. account_id={}",
                    account_id);
                return std::nullopt;
            }

            if (!stmt.Fetch()) {
                return std::nullopt;
            }

            CharacterRow row{};
            row.char_id = stmt.GetUInt64(1);
            row.account_id = stmt.GetUInt64(2);
            return row;
        }

    } // namespace

    bool AccountAuthDbRepository::VerifyPasswordHex_(
        std::string_view password,
        std::string_view password_hash_hex,
        std::string_view password_salt_hex)
    {
        // TODO:
        // 여기 반드시 네 실제 해시 로직으로 연결해야 한다.
        //
        // 현재 최신 스키마는 password_hash/password_salt를 쓰므로,
        // 기존처럼 평문 비교는 더 이상 맞지 않는다.
        //
        // 임시 규칙:
        // - hash/salt가 비어 있으면 실패 처리
        // - 실제 해시 함수 연결 전까지는 통과시키지 않음
        //
        if (password.empty()) {
            return false;
        }

        if (password_hash_hex.empty()) {
            return false;
        }

        // 예:
        // return VerifyPasswordWithSalt(password, password_hash_hex, password_salt_hex);
        (void)password_salt_hex;
        return false;
    }

    template <typename DbConnT>
    AccountAuthRequestResult AccountAuthDbRepository::ExecuteAuth(
        DbConnT& conn,
        const AccountAuthRequestJob& job)
    {
        AccountAuthRequestResult out{};
        out.sid = job.sid;
        out.serial = job.serial;
        out.request_id = job.request_id;

        const auto account = QueryAccountRow_(conn, job.login_id);
        if (!account.has_value()) {
            out.ok = false;
            out.fail_reason = "account_not_found";
            return out;
        }

        // 스키마 주석 기준:
        // 0:잠금, 1:정상, 2:휴면, 9:탈퇴
        if (account->account_state != 1) {
            out.ok = false;
            out.fail_reason = "account_not_active";
            return out;
        }

        if (!VerifyPasswordHex_(
            job.password,
            account->password_hash_hex,
            account->password_salt_hex))
        {
            out.ok = false;
            out.fail_reason = "invalid_password";
            return out;
        }

        std::optional<CharacterRow> ch;

        if (job.selected_char_id != 0) {
            ch = QueryCharacterById_(conn, job.selected_char_id);
            if (!ch.has_value()) {
                out.ok = false;
                out.fail_reason = "character_not_found";
                return out;
            }

            if (ch->account_id != account->account_id) {
                out.ok = false;
                out.fail_reason = "character_account_mismatch";
                return out;
            }
        }
        else {
            ch = QueryDefaultCharacterByAccountId_(conn, account->account_id);
            if (!ch.has_value()) {
                out.ok = false;
                out.fail_reason = "character_not_found";
                return out;
            }
        }

        out.ok = true;
        out.account_id = account->account_id;
        out.char_id = ch->char_id;
        out.fail_reason.clear();
        return out;
    }

    // 네 실제 connection 타입에 맞춰 explicit instantiation
    template AccountAuthRequestResult
        AccountAuthDbRepository::ExecuteAuth<dc::db::OdbcConnection>(
            dc::db::OdbcConnection& conn,
            const AccountAuthRequestJob& job);

} // namespace dc::account
