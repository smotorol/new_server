#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <chrono>
#include <functional>
#include <unordered_map>

#include "services/world/runtime/i_world_runtime.h"

namespace svr {

	struct DuplicateLoginLogContext
	{
		std::uint64_t trace_id = 0;

		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;

		std::uint32_t sid = 0;
		std::uint32_t serial = 0;

		std::uint32_t new_sid = 0;
		std::uint32_t new_serial = 0;

		std::uint16_t packet_kick_reason = 0;
		DuplicateSessionCause log_cause = DuplicateSessionCause::None;
		SessionKickStatCategory stat_category = SessionKickStatCategory::None;
	};

	struct SessionCloseLogContext
	{
		std::uint64_t trace_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
	};

	struct DelayedCloseKey
	{
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;

		bool operator==(const DelayedCloseKey& rhs) const noexcept
		{
			return sid == rhs.sid && serial == rhs.serial;
		}
	};

	struct DelayedCloseKeyHash
	{
		std::size_t operator()(const DelayedCloseKey& k) const noexcept
		{
			const std::uint64_t packed =
				(static_cast<std::uint64_t>(k.sid) << 32) |
				static_cast<std::uint64_t>(k.serial);
			return std::hash<std::uint64_t>{}(packed);
		}
	};

	struct DelayedCloseEntry
	{
		std::shared_ptr<boost::asio::steady_timer> timer;
		bool armed = false;
		SessionCloseLogContext log_ctx{};
	};

	struct ClosedAuthedSessionContext
	{
		UnbindAuthedWorldSessionResultKind unbind_kind =
			UnbindAuthedWorldSessionResultKind::NotFoundBySid;

		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;

		[[nodiscard]] bool removed() const noexcept
		{
			return unbind_kind == UnbindAuthedWorldSessionResultKind::Removed;
		}
	};

	struct PendingEnterWorldConsumeRequest
	{
		std::uint64_t trace_id = 0;
		std::uint64_t request_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0; // account consume success 이후 확정되는 char_id
		std::string login_session;
		std::string world_token;
		std::chrono::steady_clock::time_point issued_at{};
	};

} // namespace svr
