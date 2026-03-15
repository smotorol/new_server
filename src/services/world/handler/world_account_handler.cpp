#include "services/world/handler/world_account_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

WorldAccountHandler::WorldAccountHandler(
    RegisterAckCallback on_register_ack,
    DisconnectCallback on_disconnect)
    : on_register_ack_(std::move(on_register_ack))
    , on_disconnect_(std::move(on_disconnect))
{
}

void WorldAccountHandler::SetServerIdentity(
    std::uint32_t server_id,
    std::string server_name,
    std::string public_host,
    std::uint16_t public_port)
{
    server_id_ = server_id;
    server_name_ = std::move(server_name);
    public_host_ = std::move(public_host);
    public_port_ = public_port;
}

bool WorldAccountHandler::SendHelloRegister(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    pt_aw::WorldServerHello pkt{};
    pkt.server_id = server_id_;
    pkt.public_port = public_port_;

    std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%s", server_name_.c_str());
    std::snprintf(pkt.public_host, sizeof(pkt.public_host), "%s", public_host_.c_str());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_server_hello),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool WorldAccountHandler::DataAnalysis(
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

    switch (static_cast<pt_aw::AccountWorldMsg>(msg_type)) {
    case pt_aw::AccountWorldMsg::world_server_register_ack:
        {
            const auto* ack = proto::as<pt_aw::WorldServerRegisterAck>(pMsg, body_len);
            if (!ack) {
                spdlog::error("WorldAccountHandler invalid register_ack sid={}", n);
                return false;
            }

            if (ack->accepted == 0) {
                spdlog::warn("WorldAccountHandler register denied sid={} serial={}", n, GetLatestSerial(n));
                return true;
            }

            if (on_register_ack_) {
                on_register_ack_(
                    n,
                    GetLatestSerial(n),
                    ack->server_id,
                    ack->server_name,
                    ack->public_host,
                    ack->public_port);
            }
            return true;
        }

    default:
        spdlog::warn("WorldAccountHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void WorldAccountHandler::OnLineAccepted(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
        spdlog::error("WorldAccountHandler failed to send hello/register idx={} serial={}", dwIndex, dwSerial);
    }
}

bool WorldAccountHandler::ShouldHandleClose(
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    return GetLatestSerial(dwIndex) == dwSerial;
}

void WorldAccountHandler::OnLineClosed(
    std::uint32_t,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    if (on_disconnect_) {
        on_disconnect_(dwIndex, dwSerial);
    }
}
