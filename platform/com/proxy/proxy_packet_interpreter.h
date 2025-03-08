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
#ifndef PLATFORM_COM_PROXY_PROXY_PACKET_INTERPRETER_H_
#define PLATFORM_COM_PROXY_PROXY_PACKET_INTERPRETER_H_
#include <memory>
#include <unordered_map>

#include "platform/com/instance_identifier_container.h"
#include "platform/com/proxy/i_proxy_com_wrapper.h"
#include "platform/com/proxy/packet_interpreter.h"
#include "platform/com/types.h"

namespace platform {
namespace com {
namespace proxy {
namespace interpreter {
class ProxyPacketInterpreter {
 private:
  std::shared_ptr<wrapper::IProxyComWrapper> com_wrapper_;
  void ProceedFrame(const platform::com::IpcMsg&& msg) noexcept;

 protected:
  std::unordered_map<uint16_t, platform::com::proxy::interpreter::PacketInterpreter&>
      interpreters_list_{};
  const platform::com::InstanceIdentifierContainer container_;
  bool RegisterInterpreter(
      const uint16_t& id,
      platform::com::proxy::interpreter::PacketInterpreter&  // NOLINT
          handler_) noexcept;
  bool TransmitPacket(platform::com::IpcMsg&& packet) noexcept;  // NOLINT

 public:
  explicit ProxyPacketInterpreter(
      const platform::com::InstanceIdentifierContainer container,
      std::shared_ptr<wrapper::IProxyComWrapper> com_wrapper)
      : com_wrapper_{com_wrapper}, container_{container} {
    com_wrapper_->SetProceedFrameCallback(std::bind(
        &ProxyPacketInterpreter::ProceedFrame, this, std::placeholders::_1));
  }
  virtual ~ProxyPacketInterpreter() = default;

  friend class platform::com::proxy::interpreter::PacketInterpreter;
};
}  // namespace interpreter
}  // namespace proxy
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_PROXY_PROXY_PACKET_INTERPRETER_H_
