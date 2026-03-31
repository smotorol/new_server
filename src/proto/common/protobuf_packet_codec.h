#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/packet/msg_header.h"
#include "proto/common/packet_util.h"

#if __has_include(<google/protobuf/message.h>)
#include <google/protobuf/message.h>
#define DC_HAS_PROTOBUF_RUNTIME 1
#else
#define DC_HAS_PROTOBUF_RUNTIME 0
#endif

namespace dc::proto {

#if DC_HAS_PROTOBUF_RUNTIME

template <typename TMessage>
inline bool BuildFramedMessage(std::uint16_t msg_id, const TMessage& message, std::vector<char>& out)
{
    static_assert(std::is_base_of_v<google::protobuf::Message, TMessage> ||
                  std::is_base_of_v<google::protobuf::MessageLite, TMessage>,
                  "TMessage must be a protobuf message type");

    const auto body_size = static_cast<std::uint16_t>(message.ByteSizeLong());
    const auto header = ::proto::make_header(msg_id, body_size);
    out.resize(MSG_HEADER_SIZE + body_size);
    std::memcpy(out.data(), &header, MSG_HEADER_SIZE);
    return message.SerializeToArray(out.data() + MSG_HEADER_SIZE, body_size);
}

template <typename TMessage>
inline bool ParseBody(std::string_view body, TMessage& out)
{
    static_assert(std::is_base_of_v<google::protobuf::Message, TMessage> ||
                  std::is_base_of_v<google::protobuf::MessageLite, TMessage>,
                  "TMessage must be a protobuf message type");
    return out.ParseFromArray(body.data(), static_cast<int>(body.size()));
}

#endif

} // namespace dc::proto
