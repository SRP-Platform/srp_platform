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
#include "ara/com/skeleton/packet_interpreter.h"

#include <utility>

#include "ara/com/msg_type.h"
#include "ara/com/proxy/proxy_packet_interpreter.h"

namespace ara {
namespace com {
namespace skeleton {
namespace interpreter {
bool PacketInterpreter::TransmitPacket(ara::com::IpcMsg&& packet) noexcept {
  ara::com::IpcMsg packet_ = std::move(packet);
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
}  // namespace proxy
}  // namespace com
}  // namespace ara
