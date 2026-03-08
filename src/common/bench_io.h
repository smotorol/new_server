#pragma once

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace benchio {

	inline std::string EscapeCsv(std::string_view v)
	{
		bool need_quotes = false;
		for (char ch : v) {
			if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r' || ch == '\t') {
				need_quotes = true;
				break;
			}
		}
		if (!need_quotes) return std::string(v);

		std::string out;
		out.reserve(v.size() + 8);
		out.push_back('"');
		for (char ch : v) {
			if (ch == '"') out += "\"\"";
			else out.push_back(ch);
		}
		out.push_back('"');
		return out;
	}

	inline std::string EscapeTsv(std::string_view v)
	{
		std::string out;
		out.reserve(v.size());
		for (char ch : v) {
			switch (ch) {
			case '\t': out += ' '; break;
			case '\n': out += ' '; break;
			case '\r': break;
			default: out.push_back(ch); break;
			}
		}
		return out;
	}

	inline void AppendDelimitedRow(const std::filesystem::path& path,
		std::initializer_list<std::pair<std::string, std::string>> values,
		char delim)
	{
		const bool exists = std::filesystem::exists(path);
		std::ofstream ofs(path, std::ios::app | std::ios::binary);
		if (!ofs) return;

		auto esc = [delim](std::string_view s) {
			return (delim == ',') ? EscapeCsv(s) : EscapeTsv(s);
		};

		if (!exists) {
			bool first = true;
			for (const auto& kv : values) {
				if (!first) ofs << delim;
				ofs << esc(kv.first);
				first = false;
			}
			ofs << '\n';
		}

		bool first = true;
		for (const auto& kv : values) {
			if (!first) ofs << delim;
			ofs << esc(kv.second);
			first = false;
		}
		ofs << '\n';
	}

	inline void AppendCsvRow(const std::filesystem::path& path,
		std::initializer_list<std::pair<std::string, std::string>> values)
	{
		AppendDelimitedRow(path, values, ',');
	}

	inline void AppendTsvRow(const std::filesystem::path& path,
		std::initializer_list<std::pair<std::string, std::string>> values)
	{
		AppendDelimitedRow(path, values, '\t');
	}

} // namespace benchio
