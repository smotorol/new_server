#include "services/login/handler/login_world_handler.h"

#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/login_world_proto.h"

LoginWorldHandler::LoginWorldHandler(
    RegisterAckCallback on_register_ack,
    DisconnectCallback on_disconnect)
    : on_register_ack_(std::move(on_register_ack))
    , on_disconnect_(std::move(on_disconnect))
{
}

void LoginWorldHandler::SetServerIdentity(
    std::uint32_t server_id,
    std::string server_name,
    std::uint16_t listen_port)
{
    server_id_ = server_id;
    server_name_ = std::move(server_name);
    listen_port_ = listen_port;
}

bool LoginWorldHandler::SendHelloRegister(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    proto::internal::LoginServerHello pkt{};
    pkt.server_id = server_id_;
    pkt.listen_port = listen_port_;
    std::snprintf(
        pkt.server_name,
        sizeof(pkt.server_name),
        "%s",
        server_name_.c_str());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(proto::internal::LoginWorldMsg::login_server_hello),
        static_cast<std::uint16_t>(sizeof(pkt)));

    spdlog::info(
        "LoginWorldHandler::SendHelloRegister idx={} serial={} server_id={} server_name={} listen_port={}",
        dwIndex, dwSerial, server_id_, server_name_, listen_port_);

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool LoginWorldHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n,
    _MSG_HEADER* pMsgHeader, char* pMsg)
{
    (void)dwProID;

    if (!pMsgHeader) {
        return false;
    }

    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len =
        (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;

    switch (static_cast<proto::internal::LoginWorldMsg>(msg_type)) {
    case proto::internal::LoginWorldMsg::login_server_register_ack:
        {
            const auto* ack = proto::as<proto::internal::LoginServerRegisterAck>(pMsg, body_len);
            if (!ack) {
                spdlog::error("LoginWorldHandler invalid login_server_register_ack packet. sid={}", n);
                return false;
            }

            if (ack->accepted == 0) {
                spdlog::warn(
                    "LoginWorldHandler register denied. sid={} serial={} server_id={} server_name={}",
                    n, GetLatestSerial(n), ack->server_id, ack->server_name);
                return true;
            }

            spdlog::info(
                "LoginWorldHandler register ack. sid={} serial={} server_id={} server_name={} listen_port={}",
                n, GetLatestSerial(n), ack->server_id, ack->server_name, ack->listen_port);

            if (on_register_ack_) {
                on_register_ack_(n, GetLatestSerial(n), ack->server_id, ack->server_name, ack->listen_port);
            }
            return true;
        }

    default:
        spdlog::warn("LoginWorldHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void LoginWorldHandler::OnLineAccepted(std::uint32_t dwProID, std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    (void)dwProID;
    spdlog::info(
        "LoginWorldHandler::OnWorldConnected index={} serial={}",
        dwIndex,
        dwSerial);

    if (!SendHelloRegister(dwProID, dwIndex, dwSerial)) {
        spdlog::error(
            "LoginWorldHandler failed to send hello/register. index={} serial={}",
            dwIndex, dwSerial);
    }
}

bool LoginWorldHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    if (GetLatestSerial(dwIndex) != dwSerial) {
        spdlog::debug(
            "LoginWorldHandler stale close ignored. index={} serial={}",
            dwIndex,
            dwSerial);
        return false;
    }
    return true;
}

void LoginWorldHandler::OnLineClosed(std::uint32_t dwProID, std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    (void)dwProID;

    if (on_disconnect_) {
        on_disconnect_(dwIndex, dwSerial);
    }

    spdlog::warn(
        "LoginWorldHandler::OnWorldDisconnected index={} serial={}",
        dwIndex,
        dwSerial);
}
