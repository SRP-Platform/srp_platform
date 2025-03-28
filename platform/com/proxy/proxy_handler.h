/**
 * @file ProxyHandler.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_PROXY_PROXY_HANDLER_H_
#define PLATFORM_COM_PROXY_PROXY_HANDLER_H_

#include <memory>
#include <utility>

#include "platform/com/instance_identifier_container.h"
#include "platform/com/proxy/i_proxy_com_wrapper.h"
#include "platform/com/proxy/proxy_packet_interpreter.h"

namespace platform {
namespace com {
class ProxyHandler
    : public platform::com::proxy::interpreter::ProxyPacketInterpreter {
 private:
  // this value describe proxy ID localy in app
  const uint16_t client_id_{0U};

 public:
  explicit ProxyHandler(
      const platform::com::InstanceIdentifierContainer container,
      std::shared_ptr<proxy::wrapper::IProxyComWrapper> com_wrapper) noexcept
      : platform::com::proxy::interpreter::ProxyPacketInterpreter(
            container, std::move(com_wrapper)) {}

  explicit ProxyHandler(ProxyHandler&&) = delete;
  explicit ProxyHandler(ProxyHandler&) = delete;
  ProxyHandler operator=(ProxyHandler&) = delete;
  ProxyHandler operator=(ProxyHandler&&) = delete;

  ~ProxyHandler() noexcept {}
};
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_PROXY_PROXY_HANDLER_H_
