#include "services/world/handler/world_control_handler.h"

#include <cstdio>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/control_proto.h"
#include "services/world/runtime/world_runtime.h"

namespace pt_cw = proto::internal::control_world;

WorldControlHandler::WorldControlHandler(svr::WorldRuntime& runtime)
    : runtime_(runtime)
{
}

bool WorldControlHandler::SendRegisterAck(std::uint32_t dwProID, std::uint32_t sid, std::uint32_t serial,
    std::uint32_t server_id, std::string_view server_name, std::uint16_t listen_port, bool accepted)
{
    pt_cw::ControlServerRegisterAck ack{};
    ack.accepted = accepted ? 1 : 0; ack.server_id = server_id; ack.listen_port = listen_port;
    std::snprintf(ack.server_name, sizeof(ack.server_name), "%.*s", (int)server_name.size(), server_name.data());
    const auto h = proto::make_header((std::uint16_t)pt_cw::ControlWorldMsg::control_server_register_ack, (std::uint16_t)sizeof(ack));
    return Send(dwProID, sid, serial, h, reinterpret_cast<const char*>(&ack));
}

bool WorldControlHandler::DataAnalysis(std::uint32_t dwProID, std::uint32_t n, _MSG_HEADER* pMsgHeader, char* pMsg)
{
    if (!pMsgHeader) return false;
    const auto msg_type = proto::get_type_u16(*pMsgHeader);
    const std::size_t body_len = (pMsgHeader->m_wSize > MSG_HEADER_SIZE) ? (pMsgHeader->m_wSize - MSG_HEADER_SIZE) : 0;
    switch ((pt_cw::ControlWorldMsg)msg_type) {
    case pt_cw::ControlWorldMsg::control_server_hello: {
        const auto* req = proto::as<pt_cw::ControlServerHello>(pMsg, body_len); if (!req) return false;
        const auto serial = GetLatestSerial(n);
        runtime_.OnControlRegisteredFromHandler(n, serial, req->server_id, req->server_name, req->listen_port);
        return SendRegisterAck(dwProID, n, serial, req->server_id, req->server_name, req->listen_port, true);
    }
    default: spdlog::warn("WorldControlHandler unknown msg_type={} sid={}", msg_type, n); return false;
    }
}

void WorldControlHandler::OnLineAccepted(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    spdlog::info("WorldControlHandler::OnControlAccepted index={} serial={}", dwIndex, dwSerial);
}

bool WorldControlHandler::ShouldHandleClose(std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    return GetLatestSerial(dwIndex) == dwSerial;
}

void WorldControlHandler::OnLineClosed(std::uint32_t, std::uint32_t dwIndex, std::uint32_t dwSerial)
{
    runtime_.OnControlDisconnectedFromHandler(dwIndex, dwSerial);
    spdlog::info("WorldControlHandler::OnControlDisconnected index={} serial={}", dwIndex, dwSerial);
}
