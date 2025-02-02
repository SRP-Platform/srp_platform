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
#include "ara/com/proxy/packet_interpreter.h"

#include "ara/com/msg_type.h"
#include "ara/com/proxy/proxy_packet_interpreter.h"

namespace ara {
namespace com {
namespace proxy {
namespace interpreter {
void PacketInterpreter::TransmitPacket(ara::com::IpcMsg&& packet) noexcept {
  ara::com::IpcMsg packet_ = std::move(packet);

  handler_.TransmitPacket(std::move(packet_));
}

PacketInterpreter::PacketInterpreter(const std::string endpoint_name,
                                     ProxyPacketInterpreter& handler)
    : endpoint_id_{handler.container_.endpoints_.at(endpoint_name)
                       .endpoint_id_},
      handler_{handler} {
  handler_.RegisterInterpreter(0x01U, *this);
}

}  // namespace interpreter
}  // namespace proxy
}  // namespace com
}  // namespace ara
