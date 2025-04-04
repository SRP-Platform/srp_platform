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
#include "platform/com/proxy/proxy_packet_interpreter.h"

#include <iostream>
#include <unordered_map>
#include <utility>

#include "platform/com/proxy/packet_interpreter.h"
#include "platform/com/types.h"

namespace platform {
namespace com {
namespace proxy {
namespace interpreter {
void ProxyPacketInterpreter::ProceedFrame(
    const platform::com::IpcMsg&& msg) noexcept {
  const auto endpoint = this->interpreters_list_.find(msg.method_id_);

  if (this->interpreters_list_.end() != endpoint) {
    endpoint->second.ProceedPacket(std::move(msg));
  }
}
bool ProxyPacketInterpreter::RegisterInterpreter(
    const uint16_t& id,
    platform::com::proxy::interpreter::PacketInterpreter& handler_) noexcept {
  return this->interpreters_list_.insert({id, handler_}).second;
}
bool ProxyPacketInterpreter::TransmitPacket(
    platform::com::IpcMsg&& packet) noexcept {
  platform::com::IpcMsg packet_ = std::move(packet);
  packet_.service_id_ = this->container_.service_model_.service_id_;
  packet_.instance_id_ = this->container_.service_model_.instance_id_;
  return this->com_wrapper_->TransmitFrame(packet_);
}

ProxyPacketInterpreter::~ProxyPacketInterpreter() {
  this->com_wrapper_->Release();
}

}  // namespace interpreter
}  // namespace proxy
}  // namespace com
}  // namespace platform
