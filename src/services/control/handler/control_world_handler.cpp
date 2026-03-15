#include "services/control/handler/control_world_handler.h"

#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/control_proto.h"

namespace pt_cw = proto::internal::control_world;

ControlWorldHandler::ControlWorldHandler(
    RegisterAckCallback on_register_ack,
    DisconnectCallback on_disconnect)
    : on_register_ack_(std::move(on_register_ack))
    , on_disconnect_(std::move(on_disconnect))
{
}

void ControlWorldHandler::SetServerIdentity(
    std::uint32_t server_id,
    std::string server_name,
    std::uint16_t listen_port)
{
    server_id_ = server_id;
    server_name_ = std::move(server_name);
    listen_port_ = listen_port;
}

bool ControlWorldHandler::SendHelloRegister(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    pt_cw::ControlServerHello pkt{};
    pkt.server_id = server_id_;
    pkt.listen_port = listen_port_;
    std::snprintf(
        pkt.server_name,
        sizeof(pkt.server_name),
        "%s",
        server_name_.c_str());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_cw::ControlWorldMsg::control_server_hello),
        static_cast<std::uint16_t>(sizeof(pkt)));

    spdlog::info(
        "ControlWorldHandler::SendHelloRegister idx={} serial={} server_id={} server_name={} listen_port={}",
        dwIndex, dwSerial, server_id_, server_name_, listen_port_);

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool ControlWorldHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
    _MSG_HEADER* pMsgHeader, char* pMsg)
{
    (void)dwProID;

    if (!pMsgHeader) {
        return false;
    }

    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len =
        (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

    switch (static_cast<pt_cw::ControlWorldMsg>(msg_type)) {
    case pt_cw::ControlWorldMsg::control_server_register_ack:
        {
            const auto* ack = proto::as<pt_cw::ControlServerRegisterAck>(pMsg, body_len);
            if (!ack) {
                spdlog::error("ControlWorldHandler invalid control_server_register_ack packet. sid={}", n);
                return false;
            }

            if (ack->accepted == 0) {
                spdlog::warn(
                    "ControlWorldHandler register denied. sid={} serial={} server_id={} server_name={}",
                    n, GetLatestSerial(n), ack->server_id, ack->server_name);
                return true;
            }

            spdlog::info(
                "ControlWorldHandler register ack. sid={} serial={} server_id={} server_name={} listen_port={}",
                n, GetLatestSerial(n), ack->server_id, ack->server_name, ack->listen_port);

            if (on_register_ack_) {
                on_register_ack_(n, GetLatestSerial(n), ack->server_id, ack->server_name, ack->listen_port);
            }
            return true;
        }

    default:
        spdlog::warn("ControlWorldHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void ControlWorldHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    (void)dwProID;
    spdlog::info(
        "ControlWorldHandler::OnWorldConnected index={} serial={}",
        dwIndex,
        dwSerial);

    if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
        spdlog::error(
            "ControlWorldHandler failed to send hello/register. index={} serial={}",
            dwIndex, dwSerial);
    }
}

bool ControlWorldHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    if (GetLatestSerial(dwIndex) != dwSerial) {
        spdlog::debug(
            "ControlWorldHandler stale close ignored. index={} serial={}",
            dwIndex,
            dwSerial);
        return false;
    }
    return true;
}

void ControlWorldHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    (void)dwProID;

    if (on_disconnect_) {
        on_disconnect_(dwIndex, dwSerial);
    }

    spdlog::warn(
        "ControlWorldHandler::OnWorldDisconnected index={} serial={}",
        dwIndex,
        dwSerial);
}
