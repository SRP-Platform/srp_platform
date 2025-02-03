/**
 * @file fields.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-01-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef ARA_COM_PROXY_FIELDS_H_
#define ARA_COM_PROXY_FIELDS_H_

#include "ara/com/msg_type.h"
#include "ara/com/proxy/event.h"
#include "ara/com/types.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"
#include "ara/com/proxy/method.h"

namespace ara {
namespace com {
namespace proxy {
template <typename T>
class EmptyFields : public ara::com::proxy::Event<T> {
 private:
 public:
  explicit Fields(const std::string name,
                  interpreter::ProxyPacketInterpreter& handler)
      : ara::com::proxy::Event<T> {
    name, handler
  } noexcept
  {
  }

  explicit Fields(Fields&&) = delete;
  explicit Fields(Fields&) = delete;
  Fields operator=(Fields&) = delete;
  Fields operator=(Fields&&) = delete;

  ~Fields() noexcept {}
};

template <typename T>
class SetterFields : public ara::com::proxy::Event<T> {
 private:
 public:
  explicit Fields(const std::string name,
                  interpreter::ProxyPacketInterpreter& handler)
      : ara::com::proxy::Event<T> {
    name, handler
  } noexcept
  {
  }

  explicit Fields(Fields&&) = delete;
  explicit Fields(Fields&) = delete;
  Fields operator=(Fields&) = delete;
  Fields operator=(Fields&&) = delete;

  ~Fields() noexcept {}

  ara::core::Result<void> Set(const T& val) const noexcept {}
};

template <typename T>
class GetterFields : public ara::com::proxy::Event<T> {
 private:
 public:
  explicit Fields(const std::string name,
                  interpreter::ProxyPacketInterpreter& handler)
      : ara::com::proxy::Event<T> {
    name, handler
  } noexcept
  {
  }

  explicit Fields(Fields&&) = delete;
  explicit Fields(Fields&) = delete;
  Fields operator=(Fields&) = delete;
  Fields operator=(Fields&&) = delete;

  ~Fields() noexcept {}

  ara::core::Result<T> Get() const noexcept {}
};

template <typename T>
class Fields : public ara::com::proxy::Event<T> {
 private:
 public:
  explicit Fields(const std::string name,
                  interpreter::ProxyPacketInterpreter& handler)
      : ara::com::proxy::Event<T> {
    name, handler
  } noexcept
  {
  }

  explicit Fields(Fields&&) = delete;
  explicit Fields(Fields&) = delete;
  Fields operator=(Fields&) = delete;
  Fields operator=(Fields&&) = delete;

  ~Fields() noexcept {}

  ara::core::Result<T> Get() const noexcept {}
  ara::core::Result<void> Set(const T& val) const noexcept {}
};
}  // namespace proxy
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_PROXY_FIELDS_H_
