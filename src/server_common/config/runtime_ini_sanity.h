#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "server_common/config/aoi_config.h"

namespace dc::cfg {

	namespace detail {
		template <typename T, typename ParseFn>
		inline bool ParseOrKeepNumericImpl(
			const char* key,
			const std::string& raw,
			T& inout_value,
			bool fail_fast,
			ParseFn parse_fn,
			std::string* out_error,
			std::string* out_warn)
		{
			if (raw.empty()) {
				return true;
			}
			T parsed = inout_value;
			if (parse_fn(raw, parsed)) {
				inout_value = parsed;
				return true;
			}
			if (fail_fast) {
				if (out_error) {
					*out_error = std::string("invalid numeric config: ") + key + "='" + raw + "'";
				}
				return false;
			}
			if (out_warn) {
				*out_warn = std::string("invalid numeric config: ") + key + "='" + raw
					+ "' -> keep(" + std::to_string(inout_value) + ")";
			}
			return true;
		}
	} // namespace detail

	inline bool TryParseInt(const std::string& s, int& out) noexcept
	{
		try {
			std::size_t pos = 0;
			const int v = std::stoi(s, &pos);
			if (pos != s.size()) {
				return false;
			}
			out = v;
			return true;
		}
		catch (...) {
			return false;
		}
	}

	inline bool TryParseU32(const std::string& s, std::uint32_t& out) noexcept
	{
		try {
			std::size_t pos = 0;
			const unsigned long long v = std::stoull(s, &pos);
			if (pos != s.size() || v > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
				return false;
			}
			out = static_cast<std::uint32_t>(v);
			return true;
		}
		catch (...) {
			return false;
		}
	}

	inline bool ParseIntOrKeep(
		const char* key,
		const std::string& raw,
		int& inout_value,
		bool fail_fast,
		std::string* out_error = nullptr,
		std::string* out_warn = nullptr)
	{
		return detail::ParseOrKeepNumericImpl(
			key,
			raw,
			inout_value,
			fail_fast,
			[](const std::string& text, int& value) { return TryParseInt(text, value); },
			out_error,
			out_warn);
	}

	inline bool ParseU32OrKeep(
		const char* key,
		const std::string& raw,
		std::uint32_t& inout_value,
		bool fail_fast,
		std::string* out_error = nullptr,
		std::string* out_warn = nullptr)
	{
		return detail::ParseOrKeepNumericImpl(
			key,
			raw,
			inout_value,
			fail_fast,
			[](const std::string& text, std::uint32_t& value) { return TryParseU32(text, value); },
			out_error,
			out_warn);
	}

	inline std::uint32_t ClampU32Min(std::uint32_t v, std::uint32_t min_v, std::uint32_t fallback) noexcept
	{
		if (v < min_v) return fallback;
		return v;
	}

	inline int ClampIntMin(int v, int min_v, int fallback) noexcept
	{
		if (v < min_v) return fallback;
		return v;
	}

	inline void NormalizeShardAndRedisWait(
		std::uint32_t& db_shard_count,
		std::uint32_t& redis_shard_count,
		int& redis_wait_replicas,
		int& redis_wait_timeout_ms) noexcept
	{
		db_shard_count = ClampU32Min(db_shard_count, 1u, 1u);
		if (redis_shard_count == 0) {
			redis_shard_count = db_shard_count;
		}
		redis_shard_count = ClampU32Min(redis_shard_count, 1u, 1u);

		if (!(redis_wait_replicas > 0 && redis_wait_timeout_ms > 0)) {
			redis_wait_replicas = 0;
			redis_wait_timeout_ms = 0;
		}
	}

	inline void NormalizeAoiConfig(AoiConfig& cfg) noexcept
	{
		cfg.map_size.x = std::max(1, cfg.map_size.x);
		cfg.map_size.y = std::max(1, cfg.map_size.y);
		cfg.world_sight_unit = std::max(1, cfg.world_sight_unit);
		cfg.aoi_radius_cells = std::max(0, cfg.aoi_radius_cells);
	}

	inline bool ApplyMinPolicyU32(
		const char* key,
		std::uint32_t& value,
		std::uint32_t min_v,
		std::uint32_t fallback,
		bool fail_fast,
		std::string* out_error = nullptr)
	{
		if (value >= min_v) {
			return true;
		}
		if (fail_fast) {
			if (out_error) {
				*out_error = std::string("invalid config: ") + key + " (" + std::to_string(value)
					+ ") < min(" + std::to_string(min_v) + ")";
			}
			return false;
		}
		value = fallback;
		return true;
	}

	inline bool ApplyMinPolicyInt(
		const char* key,
		int& value,
		int min_v,
		int fallback,
		bool fail_fast,
		std::string* out_error = nullptr)
	{
		if (value >= min_v) {
			return true;
		}
		if (fail_fast) {
			if (out_error) {
				*out_error = std::string("invalid config: ") + key + " (" + std::to_string(value)
					+ ") < min(" + std::to_string(min_v) + ")";
			}
			return false;
		}
		value = fallback;
		return true;
	}

	struct MinPolicyIntSpec {
		const char* key = "";
		int* value = nullptr;
		int min_v = 0;
		int fallback = 0;
	};

	struct MinPolicyU32Spec {
		const char* key = "";
		std::uint32_t* value = nullptr;
		std::uint32_t min_v = 0;
		std::uint32_t fallback = 0;
	};

	struct MinPolicyTable {
		std::vector<MinPolicyIntSpec> int_specs;
		std::vector<MinPolicyU32Spec> u32_specs;
	};

	inline bool ApplyMinPolicies(
		std::initializer_list<MinPolicyIntSpec> int_specs,
		std::initializer_list<MinPolicyU32Spec> u32_specs,
		bool fail_fast,
		std::string* out_error = nullptr)
	{
		for (const auto& spec : int_specs) {
			if (spec.value == nullptr) {
				continue;
			}
			if (!ApplyMinPolicyInt(spec.key, *spec.value, spec.min_v, spec.fallback, fail_fast, out_error)) {
				return false;
			}
		}
		for (const auto& spec : u32_specs) {
			if (spec.value == nullptr) {
				continue;
			}
			if (!ApplyMinPolicyU32(spec.key, *spec.value, spec.min_v, spec.fallback, fail_fast, out_error)) {
				return false;
			}
		}
		return true;
	}

	inline bool ApplyMinPolicies(
		const std::vector<MinPolicyIntSpec>& int_specs,
		const std::vector<MinPolicyU32Spec>& u32_specs,
		bool fail_fast,
		std::string* out_error = nullptr)
	{
		for (const auto& spec : int_specs) {
			if (spec.value == nullptr) {
				continue;
			}
			if (!ApplyMinPolicyInt(spec.key, *spec.value, spec.min_v, spec.fallback, fail_fast, out_error)) {
				return false;
			}
		}
		for (const auto& spec : u32_specs) {
			if (spec.value == nullptr) {
				continue;
			}
			if (!ApplyMinPolicyU32(spec.key, *spec.value, spec.min_v, spec.fallback, fail_fast, out_error)) {
				return false;
			}
		}
		return true;
	}

	inline bool ValidateSchemaVersion(
		const char* key,
		int loaded_version,
		int expected_version,
		bool fail_fast,
		std::string* out_error = nullptr)
	{
		if (loaded_version == expected_version) {
			return true;
		}
		if (fail_fast) {
			if (out_error) {
				*out_error = std::string("schema version mismatch: ") + key
					+ " loaded=" + std::to_string(loaded_version)
					+ " expected=" + std::to_string(expected_version);
			}
			return false;
		}
		return true;
	}

	inline bool ValidateSchemaCompatibility(
		const char* key,
		int loaded_version,
		int expected_version,
		int min_supported_version,
		int max_supported_version,
		bool fail_fast,
		std::string* out_error = nullptr)
	{
		if (loaded_version < min_supported_version || loaded_version > max_supported_version) {
			if (fail_fast) {
				if (out_error) {
					*out_error = std::string("schema unsupported: ") + key
						+ " loaded=" + std::to_string(loaded_version)
						+ " supported=[" + std::to_string(min_supported_version)
						+ "," + std::to_string(max_supported_version) + "]";
				}
				return false;
			}
			return true;
		}
		return ValidateSchemaVersion(key, loaded_version, expected_version, fail_fast, out_error);
	}

} // namespace dc::cfg
