#pragma once

#include <cstdint>

#include "server_common/config/runtime_ini_sanity.h"

namespace dc::cfg {

	struct CommonRuntimePolicyTargets {
		int* flush_interval_sec = nullptr;
		int* char_ttl_sec = nullptr;
		int* db_pool_size_per_world = nullptr;
		std::uint32_t* flush_batch_immediate = nullptr;
		std::uint32_t* flush_batch_normal = nullptr;
	};

	struct WorldRuntimePolicyTargets : CommonRuntimePolicyTargets {
		int* reconnect_grace_close_delay_ms = nullptr;
	};

	struct CommonRuntimePolicyDefaults {
		int default_flush_interval_sec = 60;
		int default_char_ttl_sec = 60 * 60 * 24 * 7;
		int default_db_pool_size_per_world = 2;
		std::uint32_t default_batch_immediate = 500;
		std::uint32_t default_batch_normal = 200;
	};

	struct WorldRuntimePolicyDefaults : CommonRuntimePolicyDefaults {
		int default_reconnect_grace_close_delay_ms = 5000;
	};

	inline MinPolicyTable BuildChannelRuntimeMinPolicyTable(
		const CommonRuntimePolicyTargets& t,
		const CommonRuntimePolicyDefaults& d = {})
	{
		MinPolicyTable table{};
		table.int_specs = {
			{ "WRITE_BEHIND.FLUSH_INTERVAL_SEC", t.flush_interval_sec, 1, d.default_flush_interval_sec },
			{ "WRITE_BEHIND.CHAR_TTL_SEC", t.char_ttl_sec, 60, d.default_char_ttl_sec },
			{ "DB_WORK.POOL_SIZE_PER_WORLD", t.db_pool_size_per_world, 1, d.default_db_pool_size_per_world },
		};
		table.u32_specs = {
			{ "WRITE_BEHIND.FLUSH_BATCH_IMMEDIATE", t.flush_batch_immediate, 1u, d.default_batch_immediate },
			{ "WRITE_BEHIND.FLUSH_BATCH_NORMAL", t.flush_batch_normal, 1u, d.default_batch_normal },
		};
		return table;
	}

	inline MinPolicyTable BuildWorldRuntimeMinPolicyTable(
		const WorldRuntimePolicyTargets& t,
		const WorldRuntimePolicyDefaults& d = {})
	{
		MinPolicyTable table = BuildChannelRuntimeMinPolicyTable(t, d);
		table.int_specs.push_back(
			{ "SESSION.RECONNECT_GRACE_CLOSE_DELAY_MS", t.reconnect_grace_close_delay_ms, 100, d.default_reconnect_grace_close_delay_ms });
		return table;
	}

} // namespace dc::cfg

