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
#ifndef PLATFORM_COM_SKELETON_SKELETON_PACKET_INTERPRETER_H_
#define PLATFORM_COM_SKELETON_SKELETON_PACKET_INTERPRETER_H_
#include <memory>
#include <unordered_map>

#include "platform/com/instance_identifier_container.h"
#include "platform/com/log.h"
#include "platform/com/skeleton/i_skeleton_com_wrapper.h"
#include "platform/com/skeleton/packet_interpreter.h"
#include "platform/com/types.h"

namespace platform {
namespace com {
namespace skeleton {
namespace interpreter {
class SkeletonPacketInterpreter {
 protected:
  std::shared_ptr<wrapper::ISkeletonComWrapper> com_wrapper_;
  void ProceedFrame(const platform::com::IpcMsg&& msg) noexcept;

 protected:
  std::unordered_map<uint16_t,
                     platform::com::skeleton::interpreter::PacketInterpreter&>
      method_interpreters_list_{};
  const platform::com::InstanceIdentifierContainer container_;
  bool RegisterMethodInterpreter(
      const uint16_t& id,
      platform::com::skeleton::interpreter::PacketInterpreter&  // NOLINT
          handler_) noexcept;
  bool TransmitPacket(platform::com::IpcMsg&& packet) noexcept;  // NOLINT

 public:
  explicit SkeletonPacketInterpreter(
      const platform::com::InstanceIdentifierContainer container,
      std::shared_ptr<wrapper::ISkeletonComWrapper> com_wrapper)
      : com_wrapper_{com_wrapper}, container_{container} {
    com_wrapper_->SetProceedFrameCallback(std::bind(
        &SkeletonPacketInterpreter::ProceedFrame, this, std::placeholders::_1));
  }
  virtual ~SkeletonPacketInterpreter() = default;

  friend class platform::com::skeleton::interpreter::PacketInterpreter;
};
}  // namespace interpreter
}  // namespace skeleton
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_SKELETON_SKELETON_PACKET_INTERPRETER_H_
