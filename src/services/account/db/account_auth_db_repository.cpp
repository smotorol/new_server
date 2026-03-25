#include "services/account/db/account_auth_db_repository.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <ctime>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/crypto/sha256.h"

namespace dc::account {

	namespace {

		struct AccountRow final {
			std::uint64_t account_id = 0;
			std::string login_id;
			int account_state = 0;
			std::string password_hash_hex;
			std::string password_salt_hex;
		};

		struct CharacterRow final {
			std::uint64_t char_id = 0;
			std::uint64_t account_id = 0;
			std::string char_name;
			std::uint32_t level = 0;
			std::uint16_t job = 0;
			std::uint16_t tribe = 0;
			std::uint32_t appearance_code = 0;
			std::uint64_t last_login_at_epoch_sec = 0;
		};

		std::optional<AccountRow> QueryAccountRow_(
			db::OdbcConnection& conn,
			std::string_view login_id)
		{
			db::OdbcStatement stmt(conn);

			stmt.prepare(
				"SELECT TOP (1) "
				"    [account_id], "
				"    [login_id], "
				"    [account_state], "
				"    CONVERT(VARCHAR(512), [password_hash], 2), "
				"    CONVERT(VARCHAR(256), [password_salt], 2) "
				"FROM [auth].[account] "
				"WHERE [login_id] = ? "
				"  AND [is_deleted] = 0"
			);

			stmt.bind_input_string(1, std::string(login_id));
			stmt.execute();

			if (!stmt.fetch()) {
				return std::nullopt;
			}

			AccountRow row{};
			row.account_id = stmt.get_uint64(1);
			row.login_id = stmt.get_string(2);
			row.account_state = stmt.get_int(3);
			row.password_hash_hex = stmt.get_string(4);
			row.password_salt_hex = stmt.get_string(5);
			return row;
		}

		std::optional<CharacterRow> QueryCharacterById_(
			db::OdbcConnection& conn,
			std::uint64_t char_id)
		{
			db::OdbcStatement stmt(conn);

			stmt.prepare(
				"SELECT TOP (1) [char_id], [account_id], [char_name], [level], [job], [tribe], [appearance_code], [last_login_at_utc] "
				"FROM [NFX_GAME].[game].[character] "
				"WHERE [char_id] = ? "
				"  AND [char_state] = 1");

			stmt.bind_input_uint64(1, char_id);
			stmt.execute();

			if (!stmt.fetch()) {
				return std::nullopt;
			}

			CharacterRow row{};
			row.char_id = stmt.get_uint64(1);
			row.account_id = stmt.get_uint64(2);
			row.char_name = stmt.get_string(3);
			row.level = static_cast<std::uint32_t>(stmt.get_int(4));
			row.job = static_cast<std::uint16_t>(stmt.get_int(5));
			row.tribe = static_cast<std::uint16_t>(stmt.get_int(6));
			row.appearance_code = static_cast<std::uint32_t>(stmt.get_int(7));
			return row;
		}

		std::vector<CharacterRow> QueryCharactersByAccountId_(
			db::OdbcConnection& conn,
			std::uint64_t account_id)
		{
			std::vector<CharacterRow> rows;
			db::OdbcStatement stmt(conn);

			stmt.prepare(
				"SELECT TOP (8) [char_id], [account_id], [char_name], [level], [job], [tribe], [appearance_code], [last_login_at_utc] "
				"FROM [NFX_GAME].[game].[character] "
				"WHERE [account_id] = ? "
				"  AND [char_state] = 1 "
				"ORDER BY [slot_no] ASC, [char_id] ASC");

			stmt.bind_input_uint64(1, account_id);
			stmt.execute();

			while (stmt.fetch()) {
				CharacterRow row{};
				row.char_id = stmt.get_uint64(1);
				row.account_id = stmt.get_uint64(2);
				row.char_name = stmt.get_string(3);
				row.level = static_cast<std::uint32_t>(stmt.get_int(4));
				row.job = static_cast<std::uint16_t>(stmt.get_int(5));
				row.tribe = static_cast<std::uint16_t>(stmt.get_int(6));
				row.appearance_code = static_cast<std::uint32_t>(stmt.get_int(7));
				rows.push_back(std::move(row));
			}
			return rows;
		}

		int HexNibble_(char ch)
		{
			if (ch >= '0' && ch <= '9') {
				return ch - '0';
			}
			if (ch >= 'a' && ch <= 'f') {
				return 10 + (ch - 'a');
			}
			if (ch >= 'A' && ch <= 'F') {
				return 10 + (ch - 'A');
			}
			return -1;
		}

		bool HexToBytes_(std::string_view hex, std::vector<std::uint8_t>& out)
		{
			if (hex.empty()) {
				out.clear();
				return true;
			}

			if ((hex.size() % 2) != 0) {
				return false;
			}

			out.clear();
			out.reserve(hex.size() / 2);

			for (std::size_t i = 0; i < hex.size(); i += 2) {
				const int hi = HexNibble_(hex[i]);
				const int lo = HexNibble_(hex[i + 1]);
				if (hi < 0 || lo < 0) {
					out.clear();
					return false;
				}

				out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
			}

			return true;
		}

		std::string ToUpperAscii_(std::string_view s)
		{
			std::string out(s.begin(), s.end());
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
			return out;
		}

		bool MatchSha256SaltPassword_(
			std::string_view password,
			const std::vector<std::uint8_t>& salt_bytes,
			std::string_view expected_hash_hex)
		{
			std::vector<std::uint8_t> input;
			input.reserve(salt_bytes.size() + password.size());
			input.insert(input.end(), salt_bytes.begin(), salt_bytes.end());
			input.insert(input.end(), password.begin(), password.end());

			const auto digest = dc::crypto::Sha256(input);
			const auto digest_hex = dc::crypto::ToUpperHex(digest);
			return digest_hex == ToUpperAscii_(expected_hash_hex);
		}

		bool MatchSha256PasswordSalt_(
			std::string_view password,
			const std::vector<std::uint8_t>& salt_bytes,
			std::string_view expected_hash_hex)
		{
			std::vector<std::uint8_t> input;
			input.reserve(password.size() + salt_bytes.size());
			input.insert(input.end(), password.begin(), password.end());
			input.insert(input.end(), salt_bytes.begin(), salt_bytes.end());

			const auto digest = dc::crypto::Sha256(input);
			const auto digest_hex = dc::crypto::ToUpperHex(digest);
			return digest_hex == ToUpperAscii_(expected_hash_hex);
		}

	} // namespace

	bool AccountAuthDbRepository::VerifyPasswordHex_(
		std::string_view password,
		std::string_view password_hash_hex,
		std::string_view password_salt_hex)
	{
		if (password.empty()) {
			return false;
		}
		if (password_hash_hex.empty()) {
			return false;
		}

		std::vector<std::uint8_t> salt_bytes;
		if (!HexToBytes_(password_salt_hex, salt_bytes)) {
			spdlog::warn("AccountAuth VerifyPasswordHex_: invalid salt hex");
			return false;
		}

		// 1차: SHA256(salt + password)
		if (MatchSha256SaltPassword_(password, salt_bytes, password_hash_hex)) {
			return true;
		}

		// 2차: SHA256(password + salt)
		if (MatchSha256PasswordSalt_(password, salt_bytes, password_hash_hex)) {
			return true;
		}

		return false;
	}

	AccountAuthRequestResult AccountAuthDbRepository::ExecuteAuth(
		db::OdbcConnection& conn,
		const AccountAuthRequestJob& job)
	{
		AccountAuthRequestResult out{};
		out.sid = job.sid;
		out.serial = job.serial;
		out.request_id = job.request_id;

		try {
			const auto account = QueryAccountRow_(conn, job.login_id);
			if (!account.has_value()) {
				out.ok = false;
				out.fail_reason = "account_not_found";
				return out;
			}

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

			out.ok = true;
			out.account_id = account->account_id;
			out.should_update_last_login_at_utc = true;
			out.fail_reason.clear();
			return out;
		}
		catch (const std::exception& e) {
			spdlog::error("AccountAuth ExecuteAuth exception: {}", e.what());
			out.ok = false;
			out.fail_reason = "db_error";
			return out;
		}
	}

	bool AccountAuthDbRepository::UpdateLastLoginAtUtc(
		db::OdbcConnection& conn,
		std::uint64_t account_id)
	{
		static constexpr const char* kSql = R"SQL(
UPDATE auth.account
SET last_login_at_utc = SYSUTCDATETIME()
WHERE account_id = ?
  AND is_deleted = 0
)SQL";
		db::OdbcStatement stmt(conn);
		stmt.prepare(kSql);
		stmt.bind_input_uint64(1, account_id);

		return stmt.execute();
	}

	AccountCharacterListRequestResult AccountAuthDbRepository::ExecuteCharacterList(
		db::OdbcConnection& conn,
		const AccountCharacterListRequestJob& job)
	{
		AccountCharacterListRequestResult out{};
		out.sid = job.sid;
		out.serial = job.serial;
		out.trace_id = job.trace_id;
		out.request_id = job.request_id;
		out.account_id = job.account_id;

		try {
			const auto rows = QueryCharactersByAccountId_(conn, job.account_id);
			out.ok = true;
			out.characters.reserve(rows.size());
			for (const auto& row : rows) {
				AccountCharacterSummary summary{};
				summary.char_id = row.char_id;
				summary.char_name = row.char_name;
				summary.level = row.level;
				summary.job = row.job;
				summary.appearance_code = row.appearance_code;
				summary.last_login_at_epoch_sec = row.last_login_at_epoch_sec;
				out.characters.push_back(std::move(summary));
			}
			return out;
		}
		catch (const std::exception& e) {
			spdlog::error("AccountAuth ExecuteCharacterList exception: {}", e.what());
			out.ok = false;
			out.fail_reason = "db_error";
			return out;
		}
	}
} // namespace dc::account
