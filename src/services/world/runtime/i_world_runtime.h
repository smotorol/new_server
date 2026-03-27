#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "services/world/actors/world_actors.h"
#include "server_common/session/session_key.h"
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

	enum class WorldEnterStage
	{
		None = 0,
		EnterPending,
		InWorld,
		Closing,
	};

	enum class BeginEnterWorldSessionResultKind
	{
		InvalidInput = 0,
		Started,
		AlreadyPending,
		AlreadyInWorld,
		Closing,
	};

	struct BeginEnterWorldSessionResult
	{
		BeginEnterWorldSessionResultKind kind = BeginEnterWorldSessionResultKind::InvalidInput;
		WorldEnterStage stage = WorldEnterStage::None;

		[[nodiscard]] bool started() const noexcept
		{
			return kind == BeginEnterWorldSessionResultKind::Started;
		}
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
		std::uint16_t reject_result_code = 0;
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
	// 기존 IWorldRuntime 인터페이스는 제거했다.
	// 핸들러는 이제 WorldRuntime 구체 타입의 브리지 함수를 직접 호출한다.

} // namespace svr
