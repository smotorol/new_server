#include "services/login/handler/login_account_handler.h"

#include <algorithm>
#include <cstdio>
#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/login_account_proto.h"
#include "services/login/runtime/login_line_runtime.h"

namespace pt_la = proto::internal::login_account;

LoginAccountHandler::LoginAccountHandler(dc::LoginLineRuntime& runtime)
    : runtime_(runtime)
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
        static_cast<std::uint16_t>(pt_la::Msg::login_server_hello),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::SendAccountAuthRequest(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t trace_id,
    std::uint64_t request_id,
    std::string_view login_id,
    std::string_view password)
{
    pt_la::AccountAuthRequest pkt{};
    pkt.trace_id = trace_id;
    pkt.request_id = request_id;

    std::snprintf(pkt.login_id, sizeof(pkt.login_id), "%.*s",
        static_cast<int>(login_id.size()), login_id.data());
    std::snprintf(pkt.password, sizeof(pkt.password), "%.*s",
        static_cast<int>(password.size()), password.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::Msg::account_auth_request),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::SendWorldListRequest(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t trace_id,
    std::uint64_t request_id,
    std::uint64_t account_id,
    std::string_view login_session)
{
    pt_la::WorldListRequest pkt{};
    pkt.trace_id = trace_id;
    pkt.request_id = request_id;
    pkt.account_id = account_id;
    //pkt.world_id = world_id;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
        static_cast<int>(login_session.size()), login_session.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::Msg::world_list_request),
        static_cast<std::uint16_t>(sizeof(pkt)));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::SendWorldSelectRequest(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t trace_id,
    std::uint64_t request_id,
    std::uint64_t account_id,
    std::uint16_t world_id,
    std::uint16_t channel_id,
    std::string_view login_session)
{
    pt_la::WorldSelectRequest pkt{};
    pkt.trace_id = trace_id;
    pkt.request_id = request_id;
    pkt.account_id = account_id;
    pkt.world_id = world_id;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
        static_cast<int>(login_session.size()), login_session.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::Msg::world_select_request),
        static_cast<std::uint16_t>(sizeof(pkt)));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::SendCharacterListRequest(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t trace_id,
    std::uint64_t request_id,
    std::uint64_t account_id,
    std::uint16_t world_id,
    std::string_view login_session)
{
    pt_la::CharacterListRequest pkt{};
    pkt.trace_id = trace_id;
    pkt.request_id = request_id;
    pkt.account_id = account_id;
    pkt.world_id = world_id;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
        static_cast<int>(login_session.size()), login_session.data());
    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::Msg::character_list_request),
        static_cast<std::uint16_t>(sizeof(pkt)));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginAccountHandler::SendCharacterSelectRequest(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint64_t trace_id,
    std::uint64_t request_id,
    std::uint64_t account_id,
    std::uint64_t char_id,
    std::string_view login_session)
{
    pt_la::CharacterSelectRequest pkt{};
    pkt.trace_id = trace_id;
    pkt.request_id = request_id;
    pkt.account_id = account_id;
    pkt.char_id = char_id;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s",
        static_cast<int>(login_session.size()), login_session.data());
    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_la::Msg::character_select_request),
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

    switch (static_cast<pt_la::Msg>(msg_type)) {
    case pt_la::Msg::login_server_register_ack:
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

            runtime_.OnAccountRegistered(
                n,
                GetLatestSerial(n),
                ack->server_id,
                ack->server_name,
                ack->listen_port);
            return true;
        }

    case pt_la::Msg::account_auth_result:
        {
            const auto* res = proto::as<pt_la::AccountAuthResult>(pMsg, body_len);
            if (!res) {
                spdlog::error("LoginAccountHandler invalid account_auth_result sid={}", n);
                return false;
            }

            runtime_.OnAccountAuthResult(
                res->trace_id,
                res->request_id,
                res->ok != 0,
                res->account_id,
                res->char_id,
                res->login_session,
                res->world_token,
                res->world_host,
                res->world_port,
                res->fail_reason);
            return true;
        }
    case pt_la::Msg::world_list_response:
        {
            const auto* res = proto::as<pt_la::WorldListResponse>(pMsg, body_len);
            if (!res) {
                spdlog::error("LoginAccountHandler invalid world_list_response sid={}", n);
                return false;
            }
            runtime_.OnWorldListResult(
                res->trace_id,
                res->request_id,
                res->ok != 0,
                res->account_id,
                res->count,
                res->worlds,
                res->fail_reason);
            return true;
        }
    case pt_la::Msg::world_select_response:
        {
            const auto* res = proto::as<pt_la::WorldSelectResponse>(pMsg, body_len);
            if (!res) {
                spdlog::error("LoginAccountHandler invalid world_select_response sid={}", n);
                return false;
            }
            runtime_.OnWorldSelectResult(
                res->trace_id,
                res->request_id,
                res->ok != 0,
                res->account_id,
                res->world_id,
                res->login_session,
                res->world_host,
                res->world_port,
                res->fail_reason);
            return true;
        }
    case pt_la::Msg::character_list_response:
        {
            const auto* res = proto::as<pt_la::CharacterListResponse>(pMsg, body_len);
            if (!res) {
                spdlog::error("LoginAccountHandler invalid character_list_response sid={}", n);
                return false;
            }
            runtime_.OnCharacterListResult(
                res->trace_id,
                res->request_id,
                res->ok != 0,
                res->account_id,
                res->count,
                res->characters,
                res->fail_reason);
            return true;
        }
    case pt_la::Msg::character_select_response:
        {
            const auto* res = proto::as<pt_la::CharacterSelectResponse>(pMsg, body_len);
            if (!res) {
                spdlog::error("LoginAccountHandler invalid character_select_response sid={}", n);
                return false;
            }
            runtime_.OnCharacterSelectResult(
                res->trace_id,
                res->request_id,
                res->ok != 0,
                res->account_id,
                res->char_id,
                res->login_session,
                res->world_token,
                res->world_host,
                res->world_port,
                res->fail_reason);
            return true;
        }
    case pt_la::Msg::world_enter_success_notify:
        {
            const auto* req = proto::as<pt_la::WorldEnterSuccessNotify>(pMsg, body_len);
            if (!req) {
                spdlog::error("LoginAccountHandler invalid world_enter_success_notify sid={}", n);
                return false;
            }

            runtime_.OnWorldEnterSuccessNotify(
                req->trace_id,
                req->account_id,
                req->char_id,
                req->login_session,
                req->world_token);
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

    runtime_.OnAccountDisconnected(dwIndex, dwSerial);
}
