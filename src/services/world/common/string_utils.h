#pragma once

#include "services/world/runtime/i_world_runtime.h"

namespace svr {

	inline const char* ToString(UnbindAuthedWorldSessionResultKind kind) noexcept
	{
		switch (kind) {
		case UnbindAuthedWorldSessionResultKind::InvalidInput:
			return "InvalidInput";
		case UnbindAuthedWorldSessionResultKind::NotFoundBySid:
			return "NotFoundBySid";
		case UnbindAuthedWorldSessionResultKind::SerialMismatch:
			return "SerialMismatch";
		case UnbindAuthedWorldSessionResultKind::Removed:
			return "Removed";
		default:
			return "UnknownUnbindAuthedWorldSessionResult";
		}
	}

	inline const char* ToString(DuplicateSessionCause cause) noexcept
	{
		switch (cause) {
		case DuplicateSessionCause::None:
			return "None";
		case DuplicateSessionCause::DuplicateCharSession:
			return "DuplicateCharSession";
		case DuplicateSessionCause::DuplicateAccountSession:
			return "DuplicateAccountSession";
		case DuplicateSessionCause::DuplicateCharAndAccountSession:
			return "DuplicateCharAndAccountSession";
		default:
			return "UnknownDuplicateSessionCause";
		}
	}

	inline const char* ToString(SessionKickStatCategory category) noexcept
	{
		switch (category) {
		case SessionKickStatCategory::None:
			return "None";
		case SessionKickStatCategory::DuplicateChar:
			return "DuplicateChar";
		case SessionKickStatCategory::DuplicateAccount:
			return "DuplicateAccount";
		case SessionKickStatCategory::DuplicateBoth:
			return "DuplicateBoth";
		case SessionKickStatCategory::DuplicateDeduplicatedSameSession:
			return "DuplicateDeduplicatedSameSession";
		case SessionKickStatCategory::Other:
			return "Other";
		default:
			return "UnknownSessionKickStatCategory";
		}
	}

	inline const char* ToString(svr::BindAuthedWorldSessionResultKind kind) noexcept
	{
		switch (kind) {
		case svr::BindAuthedWorldSessionResultKind::InvalidInput:
			return "InvalidInput";
		case svr::BindAuthedWorldSessionResultKind::Inserted:
			return "Inserted";
		case svr::BindAuthedWorldSessionResultKind::ReplacedCharSession:
			return "ReplacedCharSession";
		case svr::BindAuthedWorldSessionResultKind::ReplacedAccountSession:
			return "ReplacedAccountSession";
		case svr::BindAuthedWorldSessionResultKind::ReplacedBoth:
			return "ReplacedBoth";
		case svr::BindAuthedWorldSessionResultKind::AlreadyBoundSameSession:
			return "AlreadyBoundSameSession";
		default:
			return "UnknownBindAuthedWorldSessionResultKind";
		}
	}


} // namespace svr
