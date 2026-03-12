#include "services/login/handler/login_handler.h"

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"

LoginHandler::LoginHandler(WorldReadyFn is_world_ready)
    : is_world_ready_(std::move(is_world_ready))
{
}

bool LoginHandler::RequireWorldReady(std::uint16_t msg_type) const noexcept
{
    (void)msg_type;
    // TODO:
    // login 클라 프로토콜이 정리되면
    // - ping / version_check / public_status 등은 false
    // - 실제 로그인/캐릭터 관련 요청은 true
    return true;
}

bool LoginHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
    _MSG_HEADER* pMsgHeader, char* pMsg)
{
    (void)dwProID;
    (void)pMsg;

    if (!pMsgHeader) {
        return false;
    }

    const auto msg_type = proto::get_type_u16(*pMsgHeader);

    if (RequireWorldReady(msg_type) && (!is_world_ready_ || !is_world_ready_())) {
        spdlog::warn(
            "LoginHandler blocked request because world is not ready. sid={} msg_type={}",
            n, msg_type);
        return true;
    }

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
