#include "odbc_wrapper.h"

#include <array>
#include <sstream>
#include <string>
#include <utility>

namespace db {
	namespace {

		void FreeHandleSafe(SQLSMALLINT handle_type, SQLHANDLE handle) noexcept
		{
			if (handle != nullptr) {
				SQLFreeHandle(handle_type, handle);
			}
		}

		void append_char_chunk(std::string& out, const char* buf, SQLLEN indicator)
		{
			if (buf == nullptr || indicator == SQL_NULL_DATA) {
				return;
			}

			if (indicator == SQL_NO_TOTAL) {
				out.append(buf);
				return;
			}

			if (indicator <= 0) {
				return;
			}

			const std::size_t byte_count = static_cast<std::size_t>(indicator);
			if (byte_count == 0) {
				return;
			}

			if (buf[byte_count - 1] == '\0') {
				out.append(buf, byte_count - 1);
			}
			else {
				out.append(buf, byte_count);
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

		const auto rc_env = SQLSetEnvAttr(
			env_,
			SQL_ATTR_ODBC_VERSION,
			reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3),
			0);

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

		const auto rc_exec = SQLExecDirectA(
			stmt,
			reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())),
			static_cast<SQLINTEGER>(sql.size()));

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

		const auto rc_exec = SQLExecDirectA(
			stmt,
			reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())),
			static_cast<SQLINTEGER>(sql.size()));

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
		const auto rc_get = SQLGetData(stmt, 1, SQL_C_SLONG, &value, sizeof(value), &indicator);
		if (!SQL_SUCCEEDED(rc_get) || indicator == SQL_NULL_DATA) {
			std::string msg = CollectDiagnostics(SQL_HANDLE_STMT, stmt, "SQLGetData(col=1)");
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			throw OdbcError(msg.empty() ? "execute_scalar_int: failed to read first column" : msg);
		}

		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return static_cast<int>(value);
	}

	std::string OdbcConnection::CollectDiagnostics(
		SQLSMALLINT handle_type,
		SQLHANDLE handle,
		std::string_view where)
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

			const auto rc = SQLGetDiagRecA(
				handle_type,
				handle,
				record_no,
				state,
				&native_error,
				text,
				static_cast<SQLSMALLINT>(sizeof(text)),
				&text_len);

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

	[[noreturn]] void OdbcConnection::ThrowDiagnostics(
		SQLSMALLINT handle_type,
		SQLHANDLE handle,
		std::string_view where)
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

	OdbcStatement::OdbcStatement(OdbcConnection& conn)
	{
		if (!conn.connected()) {
			throw OdbcError("OdbcStatement created with disconnected connection");
		}

		const auto rc_alloc = SQLAllocHandle(
			SQL_HANDLE_STMT,
			conn.native_handle(),
			&stmt_);

		if (!SQL_SUCCEEDED(rc_alloc)) {
			OdbcConnection::ThrowDiagnostics(
				SQL_HANDLE_DBC,
				conn.native_handle(),
				"SQLAllocHandle(SQL_HANDLE_STMT)");
		}
	}

	OdbcStatement::~OdbcStatement()
	{
		if (stmt_ != nullptr) {
			SQLFreeHandle(SQL_HANDLE_STMT, stmt_);
			stmt_ = nullptr;
		}
	}

	bool OdbcStatement::prepare(std::string_view sql)
	{
		const auto rc = SQLPrepareA(
			stmt_,
			reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.data())),
			static_cast<SQLINTEGER>(sql.size()));

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLPrepareA"));
		}

		SQLSMALLINT param_count = 0;
		const auto rc_num = SQLNumParams(stmt_, &param_count);
		if (!SQL_SUCCEEDED(rc_num)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLNumParams"));
		}

		bound_strings_.clear();
		bound_string_inds_.clear();
		bound_u64s_.clear();
		bound_u64_inds_.clear();

		bound_strings_.resize(static_cast<std::size_t>(param_count));
		bound_string_inds_.resize(static_cast<std::size_t>(param_count));
		bound_u64s_.resize(static_cast<std::size_t>(param_count));
		bound_u64_inds_.resize(static_cast<std::size_t>(param_count));

		return true;
	}

	bool OdbcStatement::bind_input_string(SQLUSMALLINT param_index, const std::string& value)
	{
		return bind_input_string(param_index, value, static_cast<SQLULEN>(value.size()));
	}

	bool OdbcStatement::bind_input_string(
		SQLUSMALLINT param_index,
		const std::string& value,
		SQLULEN column_size)
	{
		if (param_index == 0) {
			throw OdbcError("bind_input_string: param_index must start from 1");
		}

		const auto index = static_cast<std::size_t>(param_index - 1);

		if (index >= bound_strings_.size() || index >= bound_string_inds_.size()) {
			throw OdbcError("bind_input_string: parameter index out of range; call prepare() first");
		}

		bound_strings_[index] = value;

		auto& buf = bound_strings_[index];
		auto& ind = bound_string_inds_[index];

		ind = SQL_NTS;

		if (column_size == 0) {
			column_size = static_cast<SQLULEN>(buf.size());
		}

		const auto rc = SQLBindParameter(
			stmt_,
			param_index,
			SQL_PARAM_INPUT,
			SQL_C_CHAR,
			SQL_VARCHAR,
			column_size,
			0,
			const_cast<char*>(buf.c_str()),
			static_cast<SQLLEN>(buf.size() + 1),
			&ind);

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLBindParameter(string/utf8)"));
		}

		return true;
	}

	bool OdbcStatement::bind_input_uint64(SQLUSMALLINT param_index, std::uint64_t value)
	{
		if (param_index == 0) {
			throw OdbcError("bind_input_uint64: param_index must start from 1");
		}

		const auto index = static_cast<std::size_t>(param_index - 1);

		if (index >= bound_u64s_.size() || index >= bound_u64_inds_.size()) {
			throw OdbcError("bind_input_uint64: parameter index out of range; call prepare() first");
		}

		bound_u64s_[index] = value;
		bound_u64_inds_[index] = 0;

		auto& v = bound_u64s_[index];
		auto& ind = bound_u64_inds_[index];

		const auto rc = SQLBindParameter(
			stmt_,
			param_index,
			SQL_PARAM_INPUT,
			SQL_C_UBIGINT,
			SQL_BIGINT,
			0,
			0,
			&v,
			0,
			&ind);

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLBindParameter(uint64)"));
		}

		return true;
	}

	bool OdbcStatement::execute()
	{
		const auto rc = SQLExecute(stmt_);

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLExecute"));
		}

		return true;
	}

	bool OdbcStatement::fetch()
	{
		const auto rc = SQLFetch(stmt_);

		if (rc == SQL_NO_DATA) {
			return false;
		}

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLFetch"));
		}

		return true;
	}

	std::string OdbcStatement::get_string(SQLUSMALLINT col)
	{
		std::string result;
		std::array<char, 1024> buf{};
		SQLLEN indicator = 0;

		while (true) {
			const auto rc = SQLGetData(
				stmt_,
				col,
				SQL_C_CHAR,
				buf.data(),
				static_cast<SQLLEN>(buf.size()),
				&indicator);

			if (rc == SQL_NO_DATA) {
				break;
			}

			if (!SQL_SUCCEEDED(rc) && rc != SQL_SUCCESS_WITH_INFO) {
				throw OdbcError(
					OdbcConnection::CollectDiagnostics(
						SQL_HANDLE_STMT,
						stmt_,
						"SQLGetData(string/utf8)"));
			}

			if (indicator == SQL_NULL_DATA) {
				return {};
			}

			append_char_chunk(result, buf.data(), indicator);

			if (rc == SQL_SUCCESS) {
				break;
			}
		}

		return result;
	}

	std::uint64_t OdbcStatement::get_uint64(SQLUSMALLINT col)
	{
		std::uint64_t value = 0;
		SQLLEN indicator = 0;

		const auto rc = SQLGetData(
			stmt_,
			col,
			SQL_C_UBIGINT,
			&value,
			sizeof(value),
			&indicator);

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLGetData(uint64)"));
		}

		if (indicator == SQL_NULL_DATA) {
			return 0;
		}

		return value;
	}

	int OdbcStatement::get_int(SQLUSMALLINT col)
	{
		int value = 0;
		SQLLEN indicator = 0;

		const auto rc = SQLGetData(
			stmt_,
			col,
			SQL_C_SLONG,
			&value,
			sizeof(value),
			&indicator);

		if (!SQL_SUCCEEDED(rc)) {
			throw OdbcError(
				OdbcConnection::CollectDiagnostics(
					SQL_HANDLE_STMT,
					stmt_,
					"SQLGetData(int)"));
		}

		if (indicator == SQL_NULL_DATA) {
			return 0;
		}

		return value;
	}

} // namespace db
