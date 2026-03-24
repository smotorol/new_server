#pragma once
#include <cstdint>
#include <memory>
#include <string_view>
#include "net/packet/msg_header.h"

namespace net {

// 기존 DataAnalysis 스타일로 옮기기 쉬운 콜백 형태
struct INetHandler {
    virtual ~INetHandler() = default;

    virtual void on_connected(std::uint32_t session_index, std::uint32_t session_serial) {}
    virtual void on_disconnected(std::uint32_t session_index, std::uint32_t session_serial) {}
    virtual void on_connected_with_endpoint(std::uint32_t session_index, std::uint32_t session_serial, std::string_view remote_endpoint)
    {
        (void)remote_endpoint;
        on_connected(session_index, session_serial);
    }
    virtual void on_disconnected_with_endpoint(std::uint32_t session_index, std::uint32_t session_serial, std::string_view remote_endpoint)
    {
        (void)remote_endpoint;
        on_disconnected(session_index, session_serial);
    }

    // body: 헤더 다음부터 시작 (길이 = header->m_wSize - MSG_HEADER_SIZE)
    // 리턴 false면 세션 종료 같은 정책을 적용할 수 있음
    virtual bool on_packet(std::uint32_t session_index, std::uint32_t session_serial, const _MSG_HEADER* header, const char* body) = 0;
};

using HandlerPtr = std::shared_ptr<INetHandler>;

} // namespace net
