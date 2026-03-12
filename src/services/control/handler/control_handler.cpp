#include "services/control/handler/control_handler.h"

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"

ControlHandler::ControlHandler(WorldReadyFn is_world_ready)
    : is_world_ready_(std::move(is_world_ready))
{
}

bool ControlHandler::RequireWorldReady(std::uint16_t msg_type) const noexcept
{
    (void)msg_type;
    // TODO:
    // control 프로토콜 정리 후
    // - health / ping / server_status 등은 false
    // - 실제 world 의존 요청은 true
    return true;
}

bool ControlHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
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
            "ControlHandler blocked request because world is not ready. sid={} msg_type={}",
            n, msg_type);
        return true;
    }

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
