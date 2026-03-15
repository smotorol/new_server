#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


#include <sql.h>
#include <sqlext.h>

namespace db {

	class OdbcError : public std::runtime_error {
	public:
		explicit OdbcError(const std::string& message)
			: std::runtime_error(message) {
		}
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
		[[nodiscard]] bool connected() const noexcept { return dbc_ != nullptr; }

		[[nodiscard]] int execute_scalar_int(std::string_view sql) const;
		void execute(std::string_view sql) const;

		[[nodiscard]] SQLHDBC native_handle() const noexcept { return dbc_; }

		static std::string CollectDiagnostics(
			SQLSMALLINT handle_type,
			SQLHANDLE handle,
			std::string_view where);

		[[noreturn]] static void ThrowDiagnostics(
			SQLSMALLINT handle_type,
			SQLHANDLE handle,
			std::string_view where);

	private:
		SQLHENV env_ = nullptr;
		SQLHDBC dbc_ = nullptr;
	};

	class OdbcStatement final {
	public:
		explicit OdbcStatement(OdbcConnection& conn);
		~OdbcStatement();

		OdbcStatement(const OdbcStatement&) = delete;
		OdbcStatement& operator=(const OdbcStatement&) = delete;

		bool prepare(std::string_view sql);
		bool bind_input_string(SQLUSMALLINT param_index, const std::string& value);
		bool bind_input_string(SQLUSMALLINT param_index, const std::string& value, SQLULEN column_size);
		bool bind_input_uint64(SQLUSMALLINT param_index, std::uint64_t value);

		bool execute();
		bool fetch();

		std::string get_string(SQLUSMALLINT col);
		std::uint64_t get_uint64(SQLUSMALLINT col);
		int get_int(SQLUSMALLINT col);

	private:
		SQLHSTMT stmt_ = nullptr;

		std::vector<std::string> bound_strings_;
		std::vector<SQLLEN> bound_string_inds_;

		std::vector<std::uint64_t> bound_u64s_;
		std::vector<SQLLEN> bound_u64_inds_;
	};

	OdbcConnection connect_dsn(const std::string& connection_string);
	int execute_scalar_int(OdbcConnection& conn, std::string_view sql);
	void save_character_blob(OdbcConnection& conn, std::uint64_t char_id, const std::string& blob);

} // namespace db
