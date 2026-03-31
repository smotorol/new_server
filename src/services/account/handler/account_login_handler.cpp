#include "services/account/handler/account_login_handler.h"

#include <algorithm>
#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/login_account_proto.h"
#include "services/account/runtime/account_line_runtime.h"

namespace pt_la = proto::internal::login_account;

AccountLoginHandler::AccountLoginHandler(dc::AccountLineRuntime& runtime)
    : runtime_(runtime)
{
}

bool AccountLoginHandler::SendRegisterAck(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint8_t accepted, std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port)
{
    pt_la::LoginServerRegisterAck pkt{};
    pkt.accepted = accepted;
    pkt.server_id = server_id;
    pkt.listen_port = listen_port;
    std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%.*s", (int)server_name.size(), server_name.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::login_server_register_ack, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendAccountAuthResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
    std::uint64_t char_id, std::string_view login_session, std::string_view world_token,
    std::string_view world_host, std::string_view fail_reason, std::uint16_t world_port)
{
    pt_la::AccountAuthResult pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.ok = ok ? 1 : 0; pkt.account_id = account_id; pkt.char_id = char_id; pkt.world_port = world_port;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s", (int)login_session.size(), login_session.data());
    std::snprintf(pkt.world_token, sizeof(pkt.world_token), "%.*s", (int)world_token.size(), world_token.data());
    std::snprintf(pkt.world_host, sizeof(pkt.world_host), "%.*s", (int)world_host.size(), world_host.data());
    std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s", (int)fail_reason.size(), fail_reason.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::account_auth_result, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendWorldListResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
    std::uint16_t count, const pt_la::WorldSummary* worlds, std::string_view fail_reason)
{
    pt_la::WorldListResponse pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.ok = ok ? 1 : 0; pkt.account_id = account_id; pkt.count = count;
    if (worlds) for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_world_list_max_count); ++i) pkt.worlds[i] = worlds[i];
    std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s", (int)fail_reason.size(), fail_reason.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::world_list_response, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendWorldSelectResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
    std::uint16_t world_id,
    std::string_view login_session, std::string_view world_host, std::string_view fail_reason,
    std::uint16_t world_port)
{
    pt_la::WorldSelectResponse pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.ok = ok ? 1 : 0; pkt.account_id = account_id; pkt.world_id = world_id;  pkt.world_port = world_port;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s", (int)login_session.size(), login_session.data());
    std::snprintf(pkt.world_host, sizeof(pkt.world_host), "%.*s", (int)world_host.size(), world_host.data());
    std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s", (int)fail_reason.size(), fail_reason.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::world_select_response, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendWorldEnterSuccessNotify(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t account_id, std::uint64_t char_id,
    std::string_view login_session, std::string_view world_token)
{
    pt_la::WorldEnterSuccessNotify pkt{};
    pkt.trace_id = trace_id; pkt.account_id = account_id; pkt.char_id = char_id;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s", (int)login_session.size(), login_session.data());
    std::snprintf(pkt.world_token, sizeof(pkt.world_token), "%.*s", (int)world_token.size(), world_token.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::world_enter_success_notify, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendCharacterListResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
    std::uint16_t count, const pt_la::CharacterSummary* characters, std::string_view fail_reason)
{
    pt_la::CharacterListResponse pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.ok = ok ? 1 : 0; pkt.account_id = account_id; pkt.count = count;
    if (characters) for (std::size_t i = 0; i < std::min<std::size_t>(count, dc::k_character_list_max_count); ++i) pkt.characters[i] = characters[i];
    std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s", (int)fail_reason.size(), fail_reason.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::character_list_response, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::SendCharacterSelectResult(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial,
    std::uint64_t trace_id, std::uint64_t request_id, bool ok, std::uint64_t account_id,
    std::uint64_t char_id, std::string_view login_session, std::string_view world_token,
    std::string_view world_host, std::string_view fail_reason, std::uint16_t world_port)
{
    pt_la::CharacterSelectResponse pkt{};
    pkt.trace_id = trace_id; pkt.request_id = request_id; pkt.ok = ok ? 1 : 0; pkt.account_id = account_id; pkt.char_id = char_id; pkt.world_port = world_port;
    std::snprintf(pkt.login_session, sizeof(pkt.login_session), "%.*s", (int)login_session.size(), login_session.data());
    std::snprintf(pkt.world_token, sizeof(pkt.world_token), "%.*s", (int)world_token.size(), world_token.data());
    std::snprintf(pkt.world_host, sizeof(pkt.world_host), "%.*s", (int)world_host.size(), world_host.data());
    std::snprintf(pkt.fail_reason, sizeof(pkt.fail_reason), "%.*s", (int)fail_reason.size(), fail_reason.data());
    const auto h = proto::make_header((std::uint16_t)pt_la::Msg::character_select_response, (std::uint16_t)sizeof(pkt));
    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountLoginHandler::DataAnalysis(std::uint32_t, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
    if (!pMsgHeader) return false;
    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len = (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;
    switch ((pt_la::Msg)msg_type) {
    case pt_la::Msg::login_server_hello: {
        const auto* hello = proto::as<pt_la::LoginServerHello>(pMsg, body_len); if (!hello) return false;
        runtime_.OnLoginCoordinatorRegisteredFromHandler(n, GetLatestSerial(n), hello->server_id, hello->server_name, hello->listen_port); return true; }
    case pt_la::Msg::account_auth_request: {
        const auto* req = proto::as<pt_la::AccountAuthRequest>(pMsg, body_len); if (!req) return false;
        runtime_.OnLoginAuthRequestFromHandler(n, GetLatestSerial(n), req->trace_id, req->request_id, req->login_id, req->password); return true; }
    case pt_la::Msg::world_list_request: {
        const auto* req = proto::as<pt_la::WorldListRequest>(pMsg, body_len); if (!req) return false;
        runtime_.OnWorldListRequestFromHandler(n, GetLatestSerial(n), req->trace_id, req->request_id, req->account_id, req->login_session); return true; }
    case pt_la::Msg::world_select_request: {
        const auto* req = proto::as<pt_la::WorldSelectRequest>(pMsg, body_len); if (!req) return false;
        runtime_.OnWorldSelectRequestFromHandler(n, GetLatestSerial(n), req->trace_id, req->request_id, req->account_id, req->world_id, req->login_session); return true; }
    case pt_la::Msg::character_list_request: {
        const auto* req = proto::as<pt_la::CharacterListRequest>(pMsg, body_len); if (!req) return false;
        runtime_.OnCharacterListRequestFromHandler(n, GetLatestSerial(n), req->trace_id, req->request_id, req->account_id, req->world_id, req->login_session); return true; }
    case pt_la::Msg::character_select_request: {
        const auto* req = proto::as<pt_la::CharacterSelectRequest>(pMsg, body_len); if (!req) return false;
        runtime_.OnCharacterSelectRequestFromHandler(n, GetLatestSerial(n), req->trace_id, req->request_id, req->account_id, req->char_id, req->login_session); return true; }
    default: spdlog::warn("AccountLoginHandler unknown msg_type={} sid={}", msg_type, n); return false;
    }
}

void AccountLoginHandler::OnLineAccepted(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    spdlog::info("AccountLoginHandler::OnLoginAccepted index={} serial={}", dwIndex, dwSerial);
}

void AccountLoginHandler::OnLineClosed(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    runtime_.OnLoginCoordinatorDisconnectedFromHandler(dwIndex, dwSerial);
    spdlog::info("AccountLoginHandler::OnLoginDisconnected index={} serial={}", dwIndex, dwSerial);
}

bool AccountLoginHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    return GetLatestSerial(dwIndex) == dwSerial;
}
