#include "services/control/handler/control_world_handler.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "proto/internal/control_proto.h"
#include "proto/common/packet_util.h"

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
    proto::internal::ControlServerHello pkt{};
    pkt.server_id = server_id_;
    pkt.listen_port = listen_port_;
    std::snprintf(
        pkt.server_name,
        sizeof(pkt.server_name),
        "%s",
        server_name_.c_str());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(proto::internal::ControlWorldMsg::control_server_hello),
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
    (void)n;
    (void)pMsgHeader;
    (void)pMsg;

    // TODO:
    // control <-> world 내부 프로토콜 처리
    return false;
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
    spdlog::warn(
        "ControlWorldHandler::OnWorldDisconnected index={} serial={}",
        dwIndex,
        dwSerial);
}
