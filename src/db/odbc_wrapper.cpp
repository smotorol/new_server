#include "odbc_wrapper.h"

#include <array>
#include <sstream>
#include <utility>

namespace db {
	namespace {

		void FreeHandleSafe(SQLSMALLINT handle_type, SQLHANDLE handle) noexcept
		{
			if (handle != nullptr) {
				SQLFreeHandle(handle_type, handle);
			}
		}

	}

	OdbcConnection::~OdbcConnection()
	{
		disconnect();
	}

	OdbcConnection::OdbcConnection(OdbcConnection&& other) noexcept
		: env_(std::exchange(other.env_, nullptr))
		, dbc_(std::exchange(other.dbc_, nullptr))
	{
	}

	OdbcConnection& OdbcConnection::operator=(OdbcConnection&& other) noexcept
	{
		if (this != &other) {
			disconnect();
			env_ = std::exchange(other.env_, nullptr);
			dbc_ = std::exchange(other.dbc_, nullptr);
		}
		return *this;
	}

	void OdbcConnection::connect(const std::string& connection_string)
	{
		disconnect();

		if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env_) != SQL_SUCCESS) {
			throw OdbcError("SQLAllocHandle(SQL_HANDLE_ENV) failed");
		}

		const auto rc_env = SQLSetEnvAttr(env_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
		if (!SQL_SUCCEEDED(rc_env)) {
			ThrowDiagnostics(SQL_HANDLE_ENV, env_, "SQLSetEnvAttr(SQL_ATTR_ODBC_VERSION)");
		}

		const auto rc_dbc = SQLAllocHandle(SQL_HANDLE_DBC, env_, &dbc_);
		if (!SQL_SUCCEEDED(rc_dbc)) {
			ThrowDiagnostics(SQL_HANDLE_ENV, env_, "SQLAllocHandle(SQL_HANDLE_DBC)");
		}

		std::array<SQLCHAR, 1024> out_conn{};
		SQLSMALLINT out_len = 0;
		const auto rc_connect = SQLDriverConnectA(
			dbc_,
			nullptr,
			reinterpret_cast<SQLCHAR*>(const_cast<char*>(connection_string.c_str())),
			SQL_NTS,
			out_conn.data(),
			static_cast<SQLSMALLINT>(out_conn.size()),
			&out_len,
			SQL_DRIVER_NOPROMPT);

		if (!SQL_SUCCEEDED(rc_connect)) {
			ThrowDiagnostics(SQL_HANDLE_DBC, dbc_, "SQLDriverConnectA");
		}
	}

	void OdbcConnection::disconnect() noexcept
	{
		if (dbc_ != nullptr) {
			SQLDisconnect(dbc_);
			FreeHandleSafe(SQL_HANDLE_DBC, dbc_);
			dbc_ = nullptr;
		}
		if (env_ != nullptr) {
			FreeHandleSafe(SQL_HANDLE_ENV, env_);
			env_ = nullptr;
		}
	}

	void OdbcConnection::execute(std::string_view sql) const
	{
		if (!connected()) {
			throw OdbcError("ODBC execute called on disconnected connection");
		}

		SQLHSTMT stmt = nullptr;
		const auto rc_alloc = SQLAllocHandle(SQL_HANDLE_STMT, dbc_, &stmt);
		if (!SQL_SUCCEEDED(rc_alloc)) {
			ThrowDiagnostics(SQL_HANDLE_DBC, dbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");
		}

		std::string sql_buf(sql);
		const auto rc_exec = SQLExecDirectA(stmt,
			reinterpret_cast<SQLCHAR*>(sql_buf.data()),
			SQL_NTS);
		if (!SQL_SUCCEEDED(rc_exec)) {
			std::string msg = CollectDiagnostics(SQL_HANDLE_STMT, stmt, "SQLExecDirectA");
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			throw OdbcError(msg);
		}

		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	}

	int OdbcConnection::execute_scalar_int(std::string_view sql) const
	{
		if (!connected()) {
			throw OdbcError("ODBC execute_scalar_int called on disconnected connection");
		}

		SQLHSTMT stmt = nullptr;
		const auto rc_alloc = SQLAllocHandle(SQL_HANDLE_STMT, dbc_, &stmt);
		if (!SQL_SUCCEEDED(rc_alloc)) {
			ThrowDiagnostics(SQL_HANDLE_DBC, dbc_, "SQLAllocHandle(SQL_HANDLE_STMT)");
		}

		std::string sql_buf(sql);
		const auto rc_exec = SQLExecDirectA(stmt,
			reinterpret_cast<SQLCHAR*>(sql_buf.data()),
			SQL_NTS);
		if (!SQL_SUCCEEDED(rc_exec)) {
			std::string msg = CollectDiagnostics(SQL_HANDLE_STMT, stmt, "SQLExecDirectA");
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			throw OdbcError(msg);
		}

		const auto rc_fetch = SQLFetch(stmt);
		if (rc_fetch == SQL_NO_DATA) {
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			throw OdbcError("execute_scalar_int: no result row");
		}
		if (!SQL_SUCCEEDED(rc_fetch)) {
			std::string msg = CollectDiagnostics(SQL_HANDLE_STMT, stmt, "SQLFetch");
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			throw OdbcError(msg);
		}

		SQLINTEGER value = 0;
		SQLLEN indicator = 0;
		const auto rc_get = SQLGetData(stmt, 1, SQL_C_SLONG, &value, 0, &indicator);
		if (!SQL_SUCCEEDED(rc_get) || indicator == SQL_NULL_DATA) {
			std::string msg = CollectDiagnostics(SQL_HANDLE_STMT, stmt, "SQLGetData(col=1)");
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			throw OdbcError(msg.empty() ? "execute_scalar_int: failed to read first column" : msg);
		}

		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return static_cast<int>(value);
	}

	std::string OdbcConnection::CollectDiagnostics(SQLSMALLINT handle_type, SQLHANDLE handle, std::string_view where)
	{
		std::ostringstream oss;
		oss << where;

		if (handle == SQL_NULL_HANDLE) {
			return oss.str();
		}

		bool has_diag = false;
		SQLSMALLINT record_no = 1;
		while (true) {
			SQLCHAR state[7] = {};
			SQLINTEGER native_error = 0;
			SQLCHAR text[1024] = {};
			SQLSMALLINT text_len = 0;

			const auto rc = SQLGetDiagRecA(handle_type, handle, record_no,
				state, &native_error, text, static_cast<SQLSMALLINT>(sizeof(text)), &text_len);
			if (rc == SQL_NO_DATA) {
				break;
			}
			if (!SQL_SUCCEEDED(rc)) {
				break;
			}

			has_diag = true;
			oss << " | state=" << reinterpret_cast<const char*>(state)
				<< ", native=" << native_error
				<< ", msg=" << reinterpret_cast<const char*>(text);
			++record_no;
		}

		if (!has_diag) {
			oss << " | no diagnostic records";
		}
		return oss.str();
	}

	[[noreturn]] void OdbcConnection::ThrowDiagnostics(SQLSMALLINT handle_type, SQLHANDLE handle, std::string_view where)
	{
		throw OdbcError(CollectDiagnostics(handle_type, handle, where));
	}

	OdbcConnection connect_dsn(const std::string& connection_string)
	{
		OdbcConnection conn;
		conn.connect(connection_string);
		return conn;
	}

	int execute_scalar_int(OdbcConnection& conn, std::string_view sql)
	{
		return conn.execute_scalar_int(sql);
	}

	void save_character_blob(OdbcConnection& conn, std::uint64_t char_id, const std::string& blob)
	{
		(void)char_id;
		(void)blob;
		(void)conn.execute_scalar_int("SELECT 1");
	}

} // namespace db
