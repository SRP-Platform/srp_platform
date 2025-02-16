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
#ifndef ARA_COM_PROXY_EXAMPLE_EXAMPLE_PROXY_H_
#define ARA_COM_PROXY_EXAMPLE_EXAMPLE_PROXY_H_

#include <memory>
#include <utility>

#include "ara/com/proxy/event.h"
#include "ara/com/proxy/impl/fields.h"
#include "ara/com/proxy/impl/method.h"
#include "ara/com/proxy/proxy_handler.h"

namespace example {
class ExampleProxy : public ara::com::ProxyHandler {
 private:
  /* data */
 public:
  ExampleProxy(
      const ara::com::InstanceIdentifierContainer container,
      std::shared_ptr<ara::com::proxy::wrapper::IProxyComWrapper> com_wrapper)
      : ara::com::ProxyHandler(container, std::move(com_wrapper)) {}

  explicit ExampleProxy(ExampleProxy&&) = delete;
  explicit ExampleProxy(ExampleProxy&) = delete;
  ExampleProxy operator=(ExampleProxy&) = delete;
  ExampleProxy operator=(ExampleProxy&&) = delete;

  ~ExampleProxy() = default;
  ara::com::proxy::Event<int8_t> test_event{"test_event", *this};
  ara::com::proxy::FieldsImpl<int8_t> test_field{"test_field", *this};
  ara::com::proxy::MethodImpl<void, uint8_t> test_method{"test_method", *this};
};
}  // namespace example

#endif  // ARA_COM_PROXY_EXAMPLE_EXAMPLE_PROXY_H_
