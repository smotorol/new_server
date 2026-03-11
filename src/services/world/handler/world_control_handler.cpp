#include "services/world/handler/world_control_handler.h"

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/control_proto.h"

WorldControlHandler::WorldControlHandler(RegisterCallback on_register, UnregisterCallback on_unregister)
	: on_register_(std::move(on_register))
	, on_unregister_(std::move(on_unregister))
{
}

bool WorldControlHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
	_MSG_HEADER* pMsgHeader, char* pMsg)
{
	if (!pMsgHeader) {
		return false;
	}

	const auto msg_type = proto::get_type_u16(*pMsgHeader);
	const std::size_t body_len =
		(pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

	switch (static_cast<proto::internal::ControlWorldMsg>(msg_type)) {
	case proto::internal::ControlWorldMsg::control_server_hello:
		{
			const auto* req = proto::as<proto::internal::ControlServerHello>(pMsg, body_len);
			if (!req) {
				spdlog::error("WorldControlHandler invalid control_server_hello packet. sid={}", n);
				return false;
			}

			if (on_register_) {
				on_register_(n, GetLatestSerial(n), req->server_id, req->server_name, req->listen_port);
			}
			return true;
		}
	default:
		spdlog::warn("WorldControlHandler unknown msg_type={} sid={}", msg_type, n);
		return false;
	}
}

void WorldControlHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	if (on_unregister_) {
		on_unregister_(dwIndex, dwSerial);
	}
	spdlog::info("WorldControlHandler::OnControlAccepted index={} serial={}", dwIndex, dwSerial);
}

bool WorldControlHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug(
			"WorldControlHandler::OnControlDisconnected ignored (stale). index={} serial={}",
			dwIndex, dwSerial);
		return false;
	}
	return true;
}

void WorldControlHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info("WorldControlHandler::OnControlDisconnected index={} serial={}", dwIndex, dwSerial);
}
