/**
 * @file example_proxy.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-22
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_PROXY_EXAMPLE_EXAMPLE_PROXY_H_
#define PLATFORM_COM_PROXY_EXAMPLE_EXAMPLE_PROXY_H_

#include <memory>
#include <utility>

#include "platform/com/proxy/event.h"
#include "platform/com/proxy/impl/fields.h"
#include "platform/com/proxy/impl/method.h"
#include "platform/com/proxy/proxy_handler.h"

namespace example {
class ExampleProxy : public platform::com::ProxyHandler {
 private:
  /* data */
 public:
  ExampleProxy(
      const platform::com::InstanceIdentifierContainer container,
      std::shared_ptr<platform::com::proxy::wrapper::IProxyComWrapper> com_wrapper)
      : platform::com::ProxyHandler(container, std::move(com_wrapper)) {}

  explicit ExampleProxy(ExampleProxy&&) = delete;
  explicit ExampleProxy(ExampleProxy&) = delete;
  ExampleProxy operator=(ExampleProxy&) = delete;
  ExampleProxy operator=(ExampleProxy&&) = delete;

  ~ExampleProxy() = default;
  platform::com::proxy::Event<int8_t> test_event{"test_event", *this};
  platform::com::proxy::FieldsImpl<int8_t> test_field{"test_field", *this};
  platform::com::proxy::MethodImpl<void, uint8_t> test_method{"test_method", *this};
};
}  // namespace example

#endif  // PLATFORM_COM_PROXY_EXAMPLE_EXAMPLE_PROXY_H_
