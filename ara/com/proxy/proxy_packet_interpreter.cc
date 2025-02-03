/**
 * @file proxy_packet_interpreter.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-22
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "ara/com/proxy/proxy_packet_interpreter.h"

#include <unordered_map>

#include "ara/com/proxy/packet_interpreter.h"
#include "ara/com/types.h"

namespace ara {
namespace com {
namespace proxy {
namespace interpreter {
void ProxyPacketInterpreter::ProceedFrame(
    const ara::com::IpcMsg&& msg) noexcept {

  const auto& endpoint = this->interpreters_list_.find(msg.method_id_);
  if (this->interpreters_list_.end() != endpoint) {
     
    endpoint->second.ProceedPacket(std::move(msg));
  }
}
bool ProxyPacketInterpreter::RegisterInterpreter(
    const uint16_t& id,
    ara::com::proxy::interpreter::PacketInterpreter& handler_) noexcept {
  return this->interpreters_list_.insert({id, handler_}).second;
}
void ProxyPacketInterpreter::TransmitPacket(
    ara::com::IpcMsg&& packet) noexcept {
  ara::com::IpcMsg packet_ = std::move(packet);
  packet_.service_id_ = this->container_.service_model_.service_id_;
  packet_.instance_id_ = this->container_.service_model_.instance_id_;
  this->com_wrapper_->TransmitFrame(packet_);
}
}  // namespace interpreter
}  // namespace proxy
}  // namespace com
}  // namespace ara
