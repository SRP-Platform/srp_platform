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
#include "platform/com/skeleton/skeleton_packet_interpreter.h"

#include <unordered_map>
#include <utility>

#include "platform/com/skeleton/packet_interpreter.h"
#include "platform/com/types.h"

namespace platform {
namespace com {
namespace skeleton {
namespace interpreter {
void SkeletonPacketInterpreter::ProceedFrame(
    const platform::com::IpcMsg&& msg) noexcept {
  const auto& endpoint = this->interpreters_list_.find(msg.method_id_);
  if (this->interpreters_list_.end() != endpoint) {
    endpoint->second.ProceedPacket(std::move(msg));
  }
}
bool SkeletonPacketInterpreter::RegisterInterpreter(
    const uint16_t& id,
    platform::com::proxy::interpreter::PacketInterpreter& handler_) noexcept {
  return this->interpreters_list_.insert({id, handler_}).second;
}
bool SkeletonPacketInterpreter::TransmitPacket(
    platform::com::IpcMsg&& packet) noexcept {
  platform::com::IpcMsg packet_ = std::move(packet);
  packet_.service_id_ = this->container_.service_model_.service_id_;
  packet_.instance_id_ = this->container_.service_model_.instance_id_;
  return this->com_wrapper_->TransmitFrame(packet_);
}
}  // namespace interpreter
}  // namespace skeleton
}  // namespace com
}  // namespace platform
