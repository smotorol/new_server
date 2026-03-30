#include "services/login/handler/login_handler.h"

#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>

#include "proto/client/login_proto.h"
#include "proto/common/packet_util.h"
#include "proto/common/protobuf_packet_codec.h"
#include "services/login/runtime/login_line_runtime.h"

#if DC_HAS_PROTOBUF_RUNTIME && __has_include("proto/generated/cpp/client_login.pb.h")
#include "proto/generated/cpp/client_login.pb.h"
#define DC_LOGIN_FIRST_PATH_PROTOBUF 1
#else
#define DC_LOGIN_FIRST_PATH_PROTOBUF 0
#endif

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
#if DC_LOGIN_FIRST_PATH_PROTOBUF
            dc::proto::client::login::LoginRequest proto_req;
            if (dc::proto::ParseBody(std::string_view(pMsg, body_len), proto_req)) {
                if (!runtime_.IssueLoginRequest(
                    n,
                    GetLatestSerial(n),
                    proto_req.login_id(),
                    proto_req.password(),
                    true))
                {
                    dc::proto::client::login::LoginResult res;
                    res.set_ok(false);
                    res.set_fail_reason("account_route_not_ready");
                    std::vector<char> framed;
                    if (!dc::proto::BuildFramedMessage(
                        static_cast<std::uint16_t>(pt_l::LoginS2CMsg::login_result),
                        res,
                        framed)) {
                        return false;
                    }
                    _MSG_HEADER header{};
                    std::memcpy(&header, framed.data(), MSG_HEADER_SIZE);
                    return Send(0, n, GetLatestSerial(n), header, framed.data() + MSG_HEADER_SIZE);
                }
                return true;
            }
#endif

            const auto* req = proto::as<pt_l::C2S_login_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid login_request packet sid={}", n);
                return false;
            }

            if (!runtime_.IssueLoginRequest(
                n,
                GetLatestSerial(n),
                req->login_id,
                req->password,
                false))
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
#if DC_LOGIN_FIRST_PATH_PROTOBUF
            dc::proto::client::login::WorldListRequest proto_req;
            if (dc::proto::ParseBody(std::string_view(pMsg, body_len), proto_req)) {
                (void)proto_req;
                return runtime_.IssueWorldListRequest(n, GetLatestSerial(n), true);
            }
#endif
            const auto* req = proto::as<pt_l::C2S_world_list_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid world_list_request packet sid={}", n);
                return false;
            }
            (void)req;
            return runtime_.IssueWorldListRequest(n, GetLatestSerial(n), false);
        }
    case pt_l::LoginC2SMsg::world_select_request:
        {
#if DC_LOGIN_FIRST_PATH_PROTOBUF
            dc::proto::client::login::WorldSelectRequest proto_req;
            if (dc::proto::ParseBody(std::string_view(pMsg, body_len), proto_req)) {
                return runtime_.IssueWorldSelectRequest(
                    n,
                    GetLatestSerial(n),
                    static_cast<std::uint16_t>(proto_req.world_id()),
                    0,
                    true);
            }
#endif
            const auto* req = proto::as<pt_l::C2S_world_select_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid world_select_request packet sid={}", n);
                return false;
            }
            return runtime_.IssueWorldSelectRequest(n, GetLatestSerial(n), req->world_id, req->channel_id, false);
        }
    case pt_l::LoginC2SMsg::character_list_request:
        {
#if DC_LOGIN_FIRST_PATH_PROTOBUF
            dc::proto::client::login::CharacterListRequest proto_req;
            if (dc::proto::ParseBody(std::string_view(pMsg, body_len), proto_req)) {
                return runtime_.IssueCharacterListRequest(n, GetLatestSerial(n), true);
            }
#endif
            const auto* req = proto::as<pt_l::C2S_character_list_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid character_list_request packet sid={}", n);
                return false;
            }
            (void)req;
            return runtime_.IssueCharacterListRequest(n, GetLatestSerial(n), false);
        }
    case pt_l::LoginC2SMsg::character_select_request:
        {
#if DC_LOGIN_FIRST_PATH_PROTOBUF
            dc::proto::client::login::CharacterSelectRequest proto_req;
            if (dc::proto::ParseBody(std::string_view(pMsg, body_len), proto_req)) {
                return runtime_.IssueCharacterSelectRequest(
                    n,
                    GetLatestSerial(n),
                    proto_req.char_id(),
                    true);
            }
#endif
            const auto* req = proto::as<pt_l::C2S_character_select_request>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginHandler invalid character_select_request packet sid={}", n);
                return false;
            }
            return runtime_.IssueCharacterSelectRequest(n, GetLatestSerial(n), req->char_id, false);
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

