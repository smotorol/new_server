#include "services/login/handler/login_handler.h"

#include <spdlog/spdlog.h>

#include "proto/client/login_proto.h"
#include "proto/common/packet_util.h"
#include "services/login/runtime/login_line_runtime.h"

namespace pt_l = proto::login;

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

    switch (static_cast<pt_l::LoginC2SMsg>(msg_type)) {
    case pt_l::LoginC2SMsg::login_request:
        {
            const auto* req = proto::as<pt_l::C2S_login_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid login_request packet sid={}", n);
                return false;
            }

            if (!runtime_.IssueLoginRequest(
                n,
                GetLatestSerial(n),
                req->login_id,
                req->password))
            {
                pt_l::S2C_login_result res{};
                res.ok = 0;

                const auto h = proto::make_header(
                    static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result),
                    static_cast<std::uint16_t>(sizeof(res)));

                Send(0, n, GetLatestSerial(n), h, reinterpret_cast<const char*>(&res));
            }

            return true;
        }
    case pt_l::LoginC2SMsg::world_list_request:
        {
            const auto* req = proto::as<pt_l::C2S_world_list_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid world_list_request packet sid={}", n);
                return false;
            }
            (void)req;
            return runtime_.IssueWorldListRequest(n, GetLatestSerial(n));
        }
    case pt_l::LoginC2SMsg::world_select_request:
        {
            const auto* req = proto::as<pt_l::C2S_world_select_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid world_select_request packet sid={}", n);
                return false;
            }
            return runtime_.IssueWorldSelectRequest(n, GetLatestSerial(n), req->world_id, req->channel_id);
        }
    case pt_l::LoginC2SMsg::character_list_request:
        {
            const auto* req = proto::as<pt_l::C2S_character_list_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid character_list_request packet sid={}", n);
                return false;
            }
            (void)req;
            return runtime_.IssueCharacterListRequest(n, GetLatestSerial(n));
        }
    case pt_l::LoginC2SMsg::character_select_request:
        {
            const auto* req = proto::as<pt_l::C2S_character_select_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid character_select_request packet sid={}", n);
                return false;
            }
            return runtime_.IssueCharacterSelectRequest(n, GetLatestSerial(n), req->char_id);
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
