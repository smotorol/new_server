#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "services/world/actors/world_actors.h"
#include "db/core/dqs_results.h"
#include "proto/internal/world_zone_proto.h"

namespace pt_wz = proto::internal::world_zone;

namespace svr {

	struct WorldAuthedSession
	{
		std::uint64_t account_id = 0;
		std::uint64_t char_id = 0;
		std::uint32_t sid = 0;
		std::uint32_t serial = 0;
	};

	enum class BindAuthedWorldSessionResultKind
	{
		InvalidInput,
		Inserted,
		ReplacedCharSession,
		ReplacedAccountSession,
		ReplacedBoth,
		AlreadyBoundSameSession,
	};

	enum class UnbindAuthedWorldSessionResultKind
	{
		InvalidInput,
		NotFoundBySid,
		SerialMismatch,
		Removed,
	};

	enum class DuplicateSessionCause
	{
		None,
		DuplicateCharSession,
		DuplicateAccountSession,
		DuplicateCharAndAccountSession,
	};

	enum class SessionKickStatCategory
	{
		None,
		DuplicateChar,
		DuplicateAccount,
		DuplicateBoth,
		DuplicateDeduplicatedSameSession,
		Other,
	};

	struct BindAuthedWorldSessionResult
	{
		BindAuthedWorldSessionResultKind kind = BindAuthedWorldSessionResultKind::InvalidInput;
		DuplicateSessionCause duplicate_cause = DuplicateSessionCause::None;
		WorldAuthedSession current_session{};
		WorldAuthedSession old_char_session{};
		WorldAuthedSession old_account_session{};

		[[nodiscard]] bool has_old_char_session() const noexcept
		{
			return old_char_session.sid != 0;
		}

		[[nodiscard]] bool has_old_account_session() const noexcept
		{
			return old_account_session.sid != 0;
		}
	};

	struct UnbindAuthedWorldSessionResult
	{
		UnbindAuthedWorldSessionResultKind kind = UnbindAuthedWorldSessionResultKind::InvalidInput;
		WorldAuthedSession session{};

		[[nodiscard]] bool removed() const noexcept
		{
			return kind == UnbindAuthedWorldSessionResultKind::Removed;
		}
	};

	enum class ConsumePendingWorldAuthTicketResultKind
	{
		Ok = 0,
		TokenNotFound,
		Expired,
		ReplayDetected,
		AccountMismatch,
		CharMismatch,
		LoginSessionMismatch,
		WorldServerMismatch,
	};

	enum class AssignMapInstanceResultKind
	{
		Ok = 0,
		Pending,
		NoZoneAvailable,
		RequestSendFailed,
		ResponseTimeout,
		Rejected,
	};

	struct AssignMapInstanceResult
	{
		AssignMapInstanceResultKind kind = AssignMapInstanceResultKind::NoZoneAvailable;
		std::uint64_t request_id = 0;
		std::uint16_t zone_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;

		[[nodiscard]] bool ok() const noexcept
		{
			return kind == AssignMapInstanceResultKind::Ok;
		}
	};

	enum class MapInstanceAssignmentResultKind
	{
		InvalidInput,
		AssignedLocalFallback,
		AssignedExistingRemote,
		AssignedNewRemoteSkeleton,
		NoAvailableZone,
	};

	struct MapInstanceAssignmentResult
	{
		MapInstanceAssignmentResultKind kind = MapInstanceAssignmentResultKind::InvalidInput;
		std::uint32_t zone_server_id = 0;
		std::uint32_t map_template_id = 0;
		std::uint32_t instance_id = 0;
		std::uint32_t local_zone_id = 0;

		[[nodiscard]] bool ok() const noexcept
		{
			return kind == MapInstanceAssignmentResultKind::AssignedLocalFallback ||
				kind == MapInstanceAssignmentResultKind::AssignedExistingRemote ||
				kind == MapInstanceAssignmentResultKind::AssignedNewRemoteSkeleton;
		}
	};

	struct ConsumePendingWorldAuthTicketResult
	{
		ConsumePendingWorldAuthTicketResultKind kind =
			ConsumePendingWorldAuthTicketResultKind::TokenNotFound;

		[[nodiscard]] bool ok() const noexcept
		{
			return kind == ConsumePendingWorldAuthTicketResultKind::Ok;
		}
	};

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

		virtual bool RequestConsumeWorldAuthTicket(
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view token) = 0;

		virtual void OnWorldAuthTicketConsumeResponse(
			std::uint64_t request_id,
			svr::ConsumePendingWorldAuthTicketResultKind result_kind,
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token) = 0;

		virtual bool NotifyAccountWorldEnterSuccess(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::string_view login_session,
			std::string_view world_token) = 0;

		virtual std::uint32_t GetActiveWorldSessionCount() const = 0;
		virtual std::uint16_t GetActiveZoneCount() const = 0;

		virtual AssignMapInstanceResult AssignMapInstance(
			std::uint32_t map_template_id,
			std::uint32_t instance_id,
			bool create_if_missing,
			bool dungeon_instance) = 0;

		virtual BindAuthedWorldSessionResult BindAuthenticatedWorldSessionForLogin(
			std::uint64_t account_id,
			std::uint64_t char_id,
			std::uint32_t sid,
			std::uint32_t serial,
			std::uint16_t kick_reason) = 0;

		virtual UnbindAuthedWorldSessionResult UnbindAuthenticatedWorldSessionBySid(
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

		virtual void OnMapAssignRequest(
			std::uint32_t sid,
			std::uint32_t serial,
			const pt_wz::WorldZoneMapAssignRequest& req) = 0;
	};

} // namespace svr
