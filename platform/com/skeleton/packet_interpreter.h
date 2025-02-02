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
#ifndef PLATFORM_COM_SKELETON_PACKET_INTERPRETER_H_
#define PLATFORM_COM_SKELETON_PACKET_INTERPRETER_H_
#include <string>

#include "platform/com/model/endpoint_model.h"
#include "platform/com/msg_type.h"

namespace platform {
namespace com {
namespace skeleton {
namespace interpreter {
class SkeletonPacketInterpreter;
class PacketInterpreter {
 protected:
  const uint16_t endpoint_id_;
  const platform::com::model::EndpointMode mode_;
  virtual void ProceedPacket(const platform::com::IpcMsg& msg) noexcept = 0;
  SkeletonPacketInterpreter& handler_;
  bool TransmitPacket(platform::com::IpcMsg&& packet) noexcept;
  void RegisterAsMethod() noexcept;
 public:
  PacketInterpreter(const std::string& endpoint_name,
                    SkeletonPacketInterpreter& handler);  // NOLINT
  virtual ~PacketInterpreter() = default;
  friend class SkeletonPacketInterpreter;
};
}  // namespace interpreter
}  // namespace skeleton
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_SKELETON_PACKET_INTERPRETER_H_
