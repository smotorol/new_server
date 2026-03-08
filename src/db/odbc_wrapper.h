#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sql.h>
#include <sqlext.h>

namespace db {

	class OdbcError : public std::runtime_error {
	public:
		explicit OdbcError(const std::string& message) : std::runtime_error(message) {}
	};

	class OdbcConnection final {
	public:
		OdbcConnection() = default;
		~OdbcConnection();

		OdbcConnection(const OdbcConnection&) = delete;
		OdbcConnection& operator=(const OdbcConnection&) = delete;

		OdbcConnection(OdbcConnection&& other) noexcept;
		OdbcConnection& operator=(OdbcConnection&& other) noexcept;

		void connect(const std::string& connection_string);
		void disconnect() noexcept;
		[[nodiscard]] bool connected() const noexcept { return dbc_ != SQL_NULL_HDBC; }

		[[nodiscard]] int execute_scalar_int(std::string_view sql) const;
		void execute(std::string_view sql) const;

	private:
		SQLHENV env_ = SQL_NULL_HENV;
		SQLHDBC dbc_ = SQL_NULL_HDBC;

		static std::string CollectDiagnostics(SQLSMALLINT handle_type, SQLHANDLE handle, std::string_view where);
		[[noreturn]] static void ThrowDiagnostics(SQLSMALLINT handle_type, SQLHANDLE handle, std::string_view where);
	};

	OdbcConnection connect_dsn(const std::string& connection_string);
	int execute_scalar_int(OdbcConnection& conn, std::string_view sql);
	void save_character_blob(OdbcConnection& conn, std::uint64_t char_id, const std::string& blob);

} // namespace db
