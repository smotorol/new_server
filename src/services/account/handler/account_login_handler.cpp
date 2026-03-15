#include "services/account/handler/account_login_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/login_account_proto.h"

namespace pt_la = proto::internal::login_account;

AccountLoginHandler::AccountLoginHandler(
    RegisterHelloCallback on_register_hello,
    DisconnectCallback on_disconnect,
    AuthRequestCallback on_auth_request)
    : on_register_hello_(std::move(on_register_hello))
    , on_disconnect_(std::move(on_disconnect))
    , on_auth_request_(std::move(on_auth_request))
{
}

bool AccountLoginHandler::SendRegisterAck(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint8_t accepted,
    std::uint32_t server_id,
    std::string_view server_name,
    std::uint16_t listen_port)
{
    proto::internal::login_account::LoginServerRegisterAck pkt{};
    pkt.accepted = accepted;
    pkt.server_id = server_id;
    pkt.listen_port = listen_port;
    std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%.*s",
        static_cast<int>(server_name.size()), server_name.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::LoginAccountMsg::login_server_register_ack),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendAccountAuthResult(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t request_id,
    bool ok,
    std::uint64_t account_id,
    std::uint64_t char_id,
    std::string_view login_session,
    std::string_view world_host,
    std::string_view fail_reason,
    std::uint16_t world_port)
{
    pt_la::AccountAuthResult pkt{};
    pkt.request_id = request_id;
    pkt.ok = ok ? 1 : 0;
    pkt.account_id = account_id;
    pkt.char_id = char_id;
    pkt.world_port = world_port;

    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
        static_cast<int>(login_session.size()), login_session.data());
    std::snprintf(pkt.world_host, sizeof(pkt.world_host), "%.*s",
        static_cast<int>(world_host.size()), world_host.data());
    std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s",
        static_cast<int>(fail_reason.size()), fail_reason.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::LoginAccountMsg::account_auth_result),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::DataAnalysis(
    std::uint32_t dwProID,
    std::uint32_t n,
    _MSG_HEADER* pMsgHeader,
    char* pMsg)
{
    (void)dwProID;

    if (!pMsgHeader) {
        return false;
    }

    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len =
        (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

    switch (static_cast<pt_la::LoginAccountMsg>(msg_type)) {
    case pt_la::LoginAccountMsg::login_server_hello:
        {
            const auto* hello = proto::as<pt_la::LoginServerHello>(pMsg, body_len);
            if (!hello) {
                spdlog::error("AccountLoginHandler invalid login_server_hello sid={}", n);
                return false;
            }

            if (on_register_hello_) {
                on_register_hello_(n, GetLatestSerial(n), hello->server_id, hello->server_name, hello->listen_port);
            }
            return true;
        }

    case pt_la::LoginAccountMsg::account_auth_request:
        {
            const auto* req = proto::as<pt_la::AccountAuthRequest>(pMsg, body_len);
            if (!req) {
                spdlog::error("AccountLoginHandler invalid account_auth_request sid={}", n);
                return false;
            }

            if (on_auth_request_) {
                on_auth_request_(
                    n,
                    GetLatestSerial(n),
                    req->request_id,
                    req->login_id,
                    req->password,
                    req->selected_char_id);
            }
            return true;
        }

    default:
        spdlog::warn("AccountLoginHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void AccountLoginHandler::OnLineAccepted(
    std::uint32_t,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    spdlog::info("AccountLoginHandler accepted index={} serial={}", dwIndex, dwSerial);
}

bool AccountLoginHandler::ShouldHandleClose(
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    return GetLatestSerial(dwIndex) == dwSerial;
}

void AccountLoginHandler::OnLineClosed(
    std::uint32_t,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    if (on_disconnect_) {
        on_disconnect_(dwIndex, dwSerial);
    }
}
