#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <limits>

// clamp helpers
inline int clamp_int_min(int v, int min_v, int fallback) {
	if (v < min_v) return fallback;
	return v;

}
inline std::uint32_t clamp_u32_min(std::uint32_t v, std::uint32_t min_v, std::uint32_t fallback) {
	if (v < min_v) return fallback;
	return v;

}

// parse helpers (silent fallback)
inline std::optional<int> try_parse_int(const std::string& s) {
	try {
		if (s.empty()) return std::nullopt;
		return std::stoi(s);

	}
	catch (...) {
		return std::nullopt;

	}
}
inline std::optional<std::uint32_t> try_parse_u32(const std::string& s) {
	try {
		if (s.empty()) return std::nullopt;
		auto v = std::stoul(s);
		if (v > (unsigned long)std::numeric_limits<std::uint32_t>::max())
			return std::nullopt;
		return (std::uint32_t)v;

	}
	catch (...) {
		return std::nullopt;

	}

}

