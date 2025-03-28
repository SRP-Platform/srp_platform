/**
 * @file method_packet_interpreter.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_SKELETON_METHOD_PACKET_INTERPRETER_H_
#define PLATFORM_COM_SKELETON_METHOD_PACKET_INTERPRETER_H_

#include <string>

#include "platform/com/skeleton/packet_interpreter.h"

namespace platform {
namespace com {
namespace skeleton {
namespace interpreter {
class MethodPacketInterpreter : public PacketInterpreter {
 private:
  /* data */
 public:
  MethodPacketInterpreter(const std::string& endpoint_name,
                          SkeletonPacketInterpreter& handler)  // NOLINT
      : PacketInterpreter{endpoint_name, handler} {
    this->RegisterAsMethod();
  }
  virtual ~MethodPacketInterpreter() noexcept = default;
};
}  // namespace interpreter
}  // namespace skeleton
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_SKELETON_METHOD_PACKET_INTERPRETER_H_
