#include "services/account/handler/account_world_handler.h"

#include <cstdio>
#include <utility>

#include <spdlog/spdlog.h>

#include "proto/common/packet_util.h"
#include "proto/internal/account_world_proto.h"

namespace pt_aw = proto::internal::account_world;

AccountWorldHandler::AccountWorldHandler(
    RegisterHelloCallback on_register_hello,
    DisconnectCallback on_disconnect)
    : on_register_hello_(std::move(on_register_hello))
    , on_disconnect_(std::move(on_disconnect))
{
}

bool AccountWorldHandler::SendRegisterAck(
    std::uint32_t dwProID,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial,
    std::uint8_t accepted,
    std::uint32_t server_id,
    std::string_view server_name,
    std::string_view public_host,
    std::uint16_t public_port)
{
    pt_aw::WorldServerRegisterAck pkt{};
    pkt.accepted = accepted;
    pkt.server_id = server_id;
    pkt.public_port = public_port;

    std::snprintf(pkt.server_name, sizeof(pkt.server_name), "%.*s",
        static_cast<int>(server_name.size()), server_name.data());
    std::snprintf(pkt.public_host, sizeof(pkt.public_host), "%.*s",
        static_cast<int>(public_host.size()), public_host.data());

    const auto h = proto::make_header(
        static_cast<std::uint16_t>(pt_aw::AccountWorldMsg::world_server_register_ack),
        static_cast<std::uint16_t>(sizeof(pkt)));

    return Send(dwProID, dwIndex, dwSerial, h, reinterpret_cast<const char*>(&pkt));
}

bool AccountWorldHandler::DataAnalysis(
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
    case pt_aw::AccountWorldMsg::world_server_hello:
        {
            const auto* hello = proto::as<pt_aw::WorldServerHello>(pMsg, body_len);
            if (!hello) {
                spdlog::error("AccountWorldHandler invalid world_server_hello sid={}", n);
                return false;
            }

            if (on_register_hello_) {
                on_register_hello_(
                    n,
                    GetLatestSerial(n),
                    hello->server_id,
                    hello->server_name,
                    hello->public_host,
                    hello->public_port);
            }
            return true;
        }

    default:
        spdlog::warn("AccountWorldHandler unknown msg_type={} sid={}", msg_type, n);
        return false;
    }
}

void AccountWorldHandler::OnLineAccepted(
    std::uint32_t,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    spdlog::info("AccountWorldHandler accepted index={} serial={}", dwIndex, dwSerial);
}

bool AccountWorldHandler::ShouldHandleClose(
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    return GetLatestSerial(dwIndex) == dwSerial;
}

void AccountWorldHandler::OnLineClosed(
    std::uint32_t,
    std::uint32_t dwIndex,
    std::uint32_t dwSerial)
{
    if (on_disconnect_) {
        on_disconnect_(dwIndex, dwSerial);
    }
}
