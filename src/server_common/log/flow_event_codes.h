#pragma once

namespace dc::logevt::account {
	inline constexpr const char* kLoginCoordinatorReady = "ACC_LOGIN_READY";
	inline constexpr const char* kLoginCoordinatorDisconnected = "ACC_LOGIN_DISC";
	inline constexpr const char* kWorldRouteRegistered = "ACC_WORLD_ROUTE_REG";
	inline constexpr const char* kWorldRouteDisconnected = "ACC_WORLD_ROUTE_DISC";
	inline constexpr const char* kWorldRouteHeartbeatStale = "ACC_WORLD_ROUTE_HB_STALE";
	inline constexpr const char* kWorldRouteHeartbeatMismatch = "ACC_WORLD_ROUTE_HB_MISMATCH";
	inline constexpr const char* kAuthRejected = "ACC_AUTH_REJECT";
	inline constexpr const char* kAuthWorldRouteUnavailable = "ACC_AUTH_ROUTE_UNAVAILABLE";
	inline constexpr const char* kWorldTicketIssued = "ACC_WORLD_TICKET_ISSUED";
	inline constexpr const char* kWorldTicketConsumeAccepted = "ACC_WORLD_TICKET_CONSUMED";
	inline constexpr const char* kWorldTicketConsumeRejected = "ACC_WORLD_TICKET_REJECTED";
	inline constexpr const char* kWorldEnterNotifyDropped = "ACC_WORLD_ENTER_NOTIFY_DROP";
	inline constexpr const char* kWorldEnterNotifyRelayed = "ACC_WORLD_ENTER_NOTIFY_RELAY";
	inline constexpr const char* kWorldTicketAwaitNotifyExpired = "ACC_WORLD_TICKET_NOTIFY_EXPIRE";
	inline constexpr const char* kWorldRouteExpired = "ACC_WORLD_ROUTE_EXPIRE";
	inline constexpr const char* kUnknown = "ACC_UNKNOWN";
}

namespace dc::logevt::login {
	inline constexpr const char* kAccountRouteReady = "LGN_ACC_ROUTE_READY";
	inline constexpr const char* kAccountRouteDown = "LGN_ACC_ROUTE_DOWN";
	inline constexpr const char* kAuthReqSent = "LGN_AUTH_REQ_SENT";
	inline constexpr const char* kAuthReqDropped = "LGN_AUTH_REQ_DROPPED";
	inline constexpr const char* kAuthSuccess = "LGN_AUTH_SUCCESS";
	inline constexpr const char* kAuthFail = "LGN_AUTH_FAIL";
	inline constexpr const char* kDuplicateClose = "LGN_DUP_SESSION_CLOSE";
	inline constexpr const char* kWorldEnterNotify = "LGN_WORLD_ENTER_NOTIFY";
}

namespace dc::logevt::world {
	inline constexpr const char* kTicketConsumeReq = "WRD_TICKET_CONSUME_REQ";
	inline constexpr const char* kTicketConsumeResp = "WRD_TICKET_CONSUME_RESP";
	inline constexpr const char* kTicketConsumeDenied = "WRD_TICKET_CONSUME_DENIED";
	inline constexpr const char* kTicketConsumeBindFail = "WRD_TICKET_BIND_FAIL";
	inline constexpr const char* kAccountRouteReady = "WRD_ACC_ROUTE_READY";
	inline constexpr const char* kAccountRouteDown = "WRD_ACC_ROUTE_DOWN";
	inline constexpr const char* kAccountRouteHeartbeat = "WRD_ACC_ROUTE_HEARTBEAT";
	inline constexpr const char* kEnterNotifyRelay = "WRD_ENTER_NOTIFY_RELAY";
	inline constexpr const char* kZoneRouteReg = "WRD_ZONE_ROUTE_REG";
	inline constexpr const char* kZoneRouteHb = "WRD_ZONE_ROUTE_HB";
	inline constexpr const char* kZoneRouteUnreg = "WRD_ZONE_ROUTE_UNREG";
}
