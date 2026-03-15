#include "services/world/handler/world_login_handler.h"

#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/login_world_proto.h"

namespace pt_lw = proto::internal::login_world;

WorldLoginHandler::WorldLoginHandler(
    svr::IWorldRuntime& runtime,
    RegisterCallback on_register,
    UnregisterCallback on_unregister)
    : runtime_(runtime)
    , on_register_(std::move(on_register))
    , on_unregister_(std::move(on_unregister))
{
}

bool WorldLoginHandler::SendRegisterAck(
    std::uint32_t dwProID,
    std::uint32_t sid,
    std::uint32_t serial,
    std::uint32_t server_id,
    std::string_view server_name,
    std::uint16_t listen_port,
    bool accepted)
{
    pt_lw::LoginServerRegisterAck ack{};
    ack.accepted = accepted ? 1 : 0;
    ack.server_id = server_id;
    ack.listen_port = listen_port;
    std::snprintf(ack.server_name, sizeof(ack.server_name), "%.*s",
        static_cast<int>(server_name.size()), server_name.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_lw::LoginWorldMsg::login_server_register_ack),
        static_cast<std::uint16_t>(sizeof(ack)));

    return Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&ack));
}

bool WorldLoginHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
    _MSG_HEADER* pMsgHeader, char* pMsg)
{
    if (!pMsgHeader) {
        return false;
    }

    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len =
        (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

    switch (static_cast<pt_lw::LoginWorldMsg>(msg_type)) {
    case pt_lw::LoginWorldMsg::login_server_hello:
        {
            const auto* req = proto::as<pt_lw::LoginServerHello>(pMsg, body_len);
            if (!req) {
                spdlog::error("WorldLoginHandler invalid login_server_hello packet. sid={}", n);
                return false;
            }

            const auto serial = GetLatestSerial(n);

            if (on_register_) {
                on_register_(n, serial, req->server_id, req->server_name, req->listen_port);
            }

            if (!SendRegisterAck(dwProID, n, serial, req->server_id, req->server_name, req->listen_port, true)) {
                spdlog::error(
                    "WorldLoginHandler failed to send register ack. sid={} serial={} server_id={}",
                    n, serial, req->server_id);
            }
            return true;
        }

    case pt_lw::LoginWorldMsg::login_auth_ticket_upsert:
        {
            const auto* req = proto::as<pt_lw::LoginAuthTicketUpsert>(pMsg, body_len);
            if (!req) {
                spdlog::error("WorldLoginHandler invalid login_auth_ticket_upsert packet. sid={}", n);
                return false;
            }

            const bool ok = runtime_.UpsertPendingWorldAuthTicket(
                req->account_id,
                req->char_id,
                req->login_session,
                req->world_token,
                req->expire_at_unix_sec);

            pt_lw::LoginAuthTicketUpsertAck ack{};
            ack.accepted = ok ? 1 : 0;
            ack.account_id = req->account_id;
            ack.char_id = req->char_id;
            std::snprintf(ack.world_token, sizeof(ack.world_token), "%s", req->world_token);

            const auto h = proto::make_header(
                static_cast<std::uint16_t>(pt_lw::LoginWorldMsg::login_auth_ticket_upsert_ack),
                static_cast<std::uint16_t>(sizeof(ack)));

            const auto serial = GetLatestSerial(n);
            Send(dwProID, n, serial, h, reinterpret_cast<const char*>(&ack));
            return true;
        }

    default:
        spdlog::warn("WorldLoginHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void WorldLoginHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    (void)dwProID;
    if (on_unregister_) {
        on_unregister_(dwIndex, dwSerial);
    }
    spdlog::info("WorldLoginHandler::OnLoginAccepted index={} serial={}", dwIndex, dwSerial);
}

bool WorldLoginHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    if (GetLatestSerial(dwIndex) != dwSerial) {
        spdlog::debug(
            "WorldLoginHandler::OnLoginDisconnected ignored (stale). index={} serial={}",
            dwIndex, dwSerial);
        return false;
    }
    return true;
}

void WorldLoginHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    (void)dwProID;
    if (on_unregister_) {
        on_unregister_(dwIndex, dwSerial);
    }
    spdlog::info("WorldLoginHandler::OnLoginDisconnected index={} serial={}", dwIndex, dwSerial);
}
