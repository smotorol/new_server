#include "services/login/handler/login_handler.h"

#include <spdlog/spdlog.h>

#include "proto/client/login_proto.h"
#include "proto/common/packet_util.h"
#include "services/login/runtime/login_line_runtime.h"

LoginHandler::LoginHandler(dc::LoginLineRuntime& runtime)
    : runtime_(runtime)
{
}

bool LoginHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
    _MSG_HEADER* pMsgHeader, char* pMsg)
{
    (void)dwProID;

    if (!pMsgHeader) {
        return false;
    }

    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len =
        (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

    switch (static_cast<proto::LoginC2SMsg>(msg_type)) {
    case proto::LoginC2SMsg::login_request:
        {
            const auto* req = proto::as<proto::C2S_login_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid login_request packet sid={}", n);
                return false;
            }

            if (!runtime_.IssueLoginSuccess(
                n,
                GetLatestSerial(n),
                req->login_id,
                req->selected_char_id))
            {
                proto::S2C_login_result res{};
                res.ok = 0;

                const auto h = proto::make_header(
                    static_cast<std::uint16_t>(proto::LoginS2CMsg::login_result),
                    static_cast<std::uint16_t>(sizeof(res)));

                Send(0, n, GetLatestSerial(n), h, reinterpret_cast<const char*>(&res));
            }

            return true;
        }

    default:
        spdlog::warn("LoginHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
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
    runtime_.RemoveLoginSession(dwIndex, dwSerial);
    spdlog::info("LoginHandler::OnLoginDisconnected index={} serial={}", dwIndex, dwSerial);
}
