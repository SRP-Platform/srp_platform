/**
 * @file event_packet_interpreter.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-22
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef ARA_COM_PROXY_EVENT_PACKET_INTERPRETER_H_
#define ARA_COM_PROXY_EVENT_PACKET_INTERPRETER_H_

#include "ara/com/proxy/packet_interpreter.h"
#include "ara/com/types.h"
#include "core/data/type_converter.h"

namespace ara {
namespace com {
namespace proxy {
namespace interpreter {
class ProxyPacketInterpreter;

template <typename DataType>
class EventPacketInterpreter : protected ara::com::proxy::interpreter::PacketInterpreter {
 protected:
  void ProceedPacket(const ara::com::IpcMsg& msg) noexcept override {
    if (msg.msg_type_ == ara::com::IpcMsgType::kSubscribeAck) {
      this->SetNewSubscriptionState(ara::com::SubscriptionState::kSubscribed);
    } else if (msg.msg_type_ == ara::com::IpcMsgType::kEvent) {
      const auto value = srp::data::Convert<DataType>::Conv(msg.payload_);
      if (value.has_value()) {
        this->SetNewValue(value.value());
      }
    }
  }
  virtual void SetNewValue(const DataType& new_value) noexcept = 0;
  virtual void SetNewSubscriptionState(
      const ara::com::SubscriptionState& new_state_) noexcept = 0;

 public:
  EventPacketInterpreter(const std::string endpoint_name,
                         ProxyPacketInterpreter& handler)
      : ara::com::proxy::interpreter::PacketInterpreter(endpoint_name,
                                                        handler) {}
  virtual ~EventPacketInterpreter() = default;
};
}  // namespace interpreter
}  // namespace proxy
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_PROXY_EVENT_PACKET_INTERPRETER_H_
