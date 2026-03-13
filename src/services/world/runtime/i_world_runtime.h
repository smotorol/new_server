#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "services/world/actors/world_actors.h"
#include "db/core/dqs_results.h"

namespace svr {

	class IWorldRuntime {
	public:
		virtual ~IWorldRuntime() = default;

		virtual void PostActor(std::uint64_t actor_id, std::function<void()> fn) = 0;
		virtual void Post(std::function<void()> fn) = 0;
		virtual void PostDqsResult(svr::dqs_result::Result r) = 0;

		virtual PlayerActor& GetOrCreatePlayerActor(std::uint64_t char_id) = 0;
		virtual WorldActor& GetOrCreateWorldActor() = 0;
		virtual ZoneActor& GetOrCreateZoneActor(std::uint32_t zone_id) = 0;
		virtual void EraseActor(std::uint64_t actor_id) = 0;

		virtual std::uint64_t FindCharIdBySession(std::uint32_t sid) const = 0;
		virtual void BindSessionCharId(std::uint32_t sid, std::uint64_t char_id) = 0;
		virtual std::uint64_t UnbindSessionCharId(std::uint32_t sid) = 0;

		virtual bool UpsertPendingWorldAuthTicket(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string token,
			std::uint64_t expire_at_unix_sec) = 0;

		virtual bool ConsumePendingWorldAuthTicket(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view token) = 0;

		virtual bool ReplaceWorldSessionForCharWithKick(
			std::uint64_t char_id,
			std::uint32_t new_sid,
			std::uint32_t new_serial,
			std::uint16_t kick_reason) = 0;

		virtual void RemoveWorldSessionBinding(
			std::uint64_t char_id,
			std::uint32_t sid,
			std::uint32_t serial) = 0;
		
		virtual void CancelDelayedWorldClose(
			std::uint32_t sid,
			std::uint32_t serial) = 0;

		virtual void HandleWorldSessionClosed(
			std::uint32_t sid,
			std::uint32_t serial) = 0;

		virtual bool PushDQSData(std::uint8_t process_code, std::uint8_t qry_case, const char* data, int size) = 0;

		virtual void CacheCharacterState(std::uint32_t world_code, std::uint64_t char_id, const std::string& blob) = 0;
		virtual std::optional<std::string> TryLoadCharacterState(std::uint32_t world_code, std::uint64_t char_id) = 0;
		virtual void RequestFlushCharacter(std::uint32_t world_code, std::uint64_t char_id) = 0;

		virtual void RequestBenchReset() noexcept = 0;
		virtual void RequestBenchMeasure(int seconds) noexcept = 0;
	};

} // namespace svr
