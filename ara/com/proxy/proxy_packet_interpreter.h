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
#ifndef ARA_COM_PROXY_PROXY_PACKET_INTERPRETER_H_
#define ARA_COM_PROXY_PROXY_PACKET_INTERPRETER_H_
#include <memory>
#include <unordered_map>

#include "ara/com/instance_identifier_container.h"
#include "ara/com/proxy/i_proxy_com_wrapper.h"
#include "ara/com/proxy/packet_interpreter.h"
#include "ara/com/types.h"

namespace ara {
namespace com {
namespace proxy {
namespace interpreter {
class ProxyPacketInterpreter {
 protected:
  std::unordered_map<uint16_t, ara::com::proxy::interpreter::PacketInterpreter&>
      interpreters_list_{};
  const ara::com::InstanceIdentifierContainer container_;
  bool RegisterInterpreter(
      const uint16_t& id,
      ara::com::proxy::interpreter::PacketInterpreter& handler_) noexcept;
  void TransmitPacket(ara::com::IpcMsg&& packet) noexcept;

 public:
  explicit ProxyPacketInterpreter(
      const ara::com::InstanceIdentifierContainer container,
      std::shared_ptr<wrapper::IProxyComWrapper> com_wrapper)
      : container_{container} {}
  virtual ~ProxyPacketInterpreter() = default;

  friend class ara::com::proxy::interpreter::PacketInterpreter;
};
}  // namespace interpreter
}  // namespace proxy
}  // namespace com
}  // namespace ara
#endif  // ARA_COM_PROXY_PROXY_PACKET_INTERPRETER_H_
