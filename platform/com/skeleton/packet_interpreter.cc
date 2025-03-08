/**
 * @file packet_interpreter.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-22
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "platform/com/skeleton/packet_interpreter.h"

#include <utility>

#include "platform/com/msg_type.h"
#include "platform/com/proxy/proxy_packet_interpreter.h"

namespace platform {
namespace com {
namespace skeleton {
namespace interpreter {
bool PacketInterpreter::TransmitPacket(
    platform::com::IpcMsg&& packet) noexcept {
  platform::com::IpcMsg packet_ = std::move(packet);
  packet_.method_id_ = this->endpoint_id_;
  return handler_.TransmitPacket(std::move(packet_));
}

PacketInterpreter::PacketInterpreter(const std::string& endpoint_name,
                                     SkeletonPacketInterpreter& handler)
    : endpoint_id_{handler.container_.endpoints_.at(endpoint_name)
                       .endpoint_id_},
      mode_{handler.container_.endpoints_.at(endpoint_name).mode_},
      handler_{handler} {
  handler_.RegisterInterpreter(endpoint_id_, *this);
}

}  // namespace interpreter
}  // namespace skeleton
}  // namespace com
}  // namespace platform
