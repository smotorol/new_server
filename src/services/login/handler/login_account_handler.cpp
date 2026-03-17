#include "services/login/handler/login_account_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/login_account_proto.h"

namespace pt_la = proto::internal::login_account;

LoginAccountHandler::LoginAccountHandler(
	RegisterAckCallback on_register_ack,
	DisconnectCallback on_disconnect,
	AuthResultCallback on_auth_result,
	EnterWorldSuccessCallback on_enter_world_success)
	: on_register_ack_(std::move(on_register_ack))
	, on_disconnect_(std::move(on_disconnect))
	, on_auth_result_(std::move(on_auth_result))
	, on_enter_world_success_(std::move(on_enter_world_success))
{
}

void LoginAccountHandler::SetServerIdentity(
    std::uint32_t server_id,
    std::string server_name,
    std::uint16_t listen_port)
{
    server_id_ = server_id;
    server_name_ = std::move(server_name);
    listen_port_ = listen_port;
}

bool LoginAccountHandler::SendHelloRegister(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    pt_la::LoginServerHello pkt{};
    pkt.server_id = server_id_;
    pkt.listen_port = listen_port_;
    std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%s", server_name_.c_str());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::LoginAccountMsg::login_server_hello),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::SendAccountAuthRequest(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t request_id,
    std::string_view login_id,
    std::string_view password,
    std::uint64_t selected_char_id)
{
    pt_la::AccountAuthRequest pkt{};
    pkt.request_id = request_id;
    pkt.selected_char_id = selected_char_id;

    std::snprintf(pkt.login_id, sizeof(pkt.login_id), "%.*s",
        static_cast<int>(login_id.size()), login_id.data());
    std::snprintf(pkt.password, sizeof(pkt.password), "%.*s",
        static_cast<int>(password.size()), password.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::LoginAccountMsg::account_auth_request),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::DataAnalysis(
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
    case pt_la::LoginAccountMsg::login_server_register_ack:
        {
            const auto* ack = proto::as<pt_la::LoginServerRegisterAck>(pMsg, body_len);
            if (!ack) {
                spdlog::error("LoginAccountHandler invalid register_ack sid={}", n);
                return false;
            }

            if (ack->accepted == 0) {
                spdlog::warn("LoginAccountHandler register denied sid={} serial={}", n, GetLatestSerial(n));
                return true;
            }

            if (on_register_ack_) {
                on_register_ack_(n, GetLatestSerial(n), ack->server_id, ack->server_name, ack->listen_port);
            }
            return true;
        }

    case pt_la::LoginAccountMsg::account_auth_result:
        {
            const auto* res = proto::as<pt_la::AccountAuthResult>(pMsg, body_len);
            if (!res) {
                spdlog::error("LoginAccountHandler invalid account_auth_result sid={}", n);
                return false;
            }

            if (on_auth_result_) {
                on_auth_result_(
                    res->request_id,
                    res->ok != 0,
                    res->account_id,
                    res->char_id,
                    res->login_session,
                    res->world_token,
                    res->world_host,
                    res->world_port,
                    res->fail_reason);
            }
            return true;
        }
    case pt_la::LoginAccountMsg::world_enter_success_notify:
        {
            const auto* req = proto::as<pt_la::WorldEnterSuccessNotify>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginAccountHandler invalid world_enter_success_notify sid={}", n);
                return false;
            }

            if (on_enter_world_success_) {
                on_enter_world_success_(
                    req->account_id,
                    req->char_id,
                    req->login_session,
                    req->world_token);
            }
            return true;
        }

    default:
        spdlog::warn("LoginAccountHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void LoginAccountHandler::OnLineAccepted(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
        spdlog::error("LoginAccountHandler failed to send hello/register idx={} serial={}", dwIndex, dwSerial);
    }
}

bool LoginAccountHandler::ShouldHandleClose(
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    return GetLatestSerial(dwIndex) == dwSerial;
}

void LoginAccountHandler::OnLineClosed(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    (void)dwProID;

    if (on_disconnect_) {
        on_disconnect_(dwIndex, dwSerial);
    }
}
