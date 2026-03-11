#include "services/control/handler/control_handler.h"

#include <spdlog/spdlog.h>

bool ControlHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
	_MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)dwProID; (void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 Control line 처리 이식
	return false;
}

void ControlHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info("ControlHandler::OnControlAccepted index={} serial={}", dwIndex, dwSerial);
}

bool ControlHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug("ControlHandler::OnControlDisconnected ignored (stale). index={} serial={}", dwIndex, dwSerial);
		return false;
	}
	return true;
}

void ControlHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info("ControlHandler::OnControlDisconnected index={} serial={}", dwIndex, dwSerial);
}
