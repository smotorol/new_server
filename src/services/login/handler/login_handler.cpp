#include "services/login/handler/login_handler.h"

#include <spdlog/spdlog.h>

bool LoginHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,

	_MSG_HEADER* pMsgHeader, char* pMsg)
{
	(void)dwProID; (void)n; (void)pMsgHeader; (void)pMsg;
	// TODO: 레거시 Login line 처리 이식
	return false;
}

void LoginHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info("LoginHandler::OnLoginAccepted index={} serial={}", dwIndex, dwSerial);
}

bool LoginHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	if (GetLatestSerial(dwIndex) != dwSerial) {
		spdlog::debug("LoginHandler::OnLoginDisconnected ignored (stale). index={} serial={}", dwIndex, dwSerial);
		return false;
	}
	return true;
}

void LoginHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
	(void)dwProID;
	spdlog::info("LoginHandler::OnLoginDisconnected index={} serial={}", dwIndex, dwSerial);
}
