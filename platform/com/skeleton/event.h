/**
 * @file event.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_SKELETON_EVENT_H_
#define PLATFORM_COM_SKELETON_EVENT_H_

#include <string>
#include <utility>

#include "core/data/type_converter.h"
#include "platform/com/message_error_domain.h"
#include "platform/com/msg_type.h"
#include "platform/com/skeleton/packet_interpreter.h"
#include "platform/core/result.h"

namespace platform {
namespace com {
namespace skeleton {
template <typename Type>
class Event final : public interpreter::PacketInterpreter {
 public:
  using SampleType = Type;

 protected:
  void ProceedPacket(const platform::com::IpcMsg& msg) noexcept {}

 public:
  platform::core::Result<void> Send(const SampleType& data) noexcept {
    const auto payload = srp::data::Convert2Vector<SampleType>::Conv(data);
    IpcMsg msg{IpcMsgType::kEvent, 0, 0, 0, 0, 0, payload.size(), payload};
    if (this->TransmitPacket(std::move(msg))) {
      return {};
    }
    return platform::com::internal::MakeErrorCode(
        platform::com::internal::MessageErrorCode::kEGeneralError,
        "Sending Event Error");
  }
  Event(const std::string& endpoint_name,
        interpreter::SkeletonPacketInterpreter& handler)  // NOLINT
      : interpreter::PacketInterpreter{endpoint_name, handler} {
    IpcMsg msg{IpcMsgType::kObjInit, 0, 0, 0, 0, 0, 1, {0x01}};
    this->TransmitPacket(std::move(msg));
  }
};
}  // namespace skeleton
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_SKELETON_EVENT_H_
