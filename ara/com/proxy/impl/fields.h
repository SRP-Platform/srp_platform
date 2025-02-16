/**
 * @file fields.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-02-16
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef ARA_COM_PROXY_IMPL_FIELDS_H_
#define ARA_COM_PROXY_IMPL_FIELDS_H_
#include <string>
#include <string_view>
#include <type_traits>

#include "ara/com/proxy/fields.h"
#include "ara/com/proxy/impl/method.h"
namespace ara {
namespace com {
namespace proxy {
template <typename Data_t, bool IsSetter = false>
class FieldsImpl final : public ara::com::proxy::Fields<Data_t> {
 private:
  /* data */
 public:
  FieldsImpl(const std::string& name,
             interpreter::ProxyPacketInterpreter& handler)  // NOLINT
      : ara::com::proxy::Fields<Data_t>{name, handler},
        Get{name + "/get", handler} {}
  ~FieldsImpl() = default;
  explicit FieldsImpl(FieldsImpl&&) = delete;
  explicit FieldsImpl(FieldsImpl&) = delete;
  FieldsImpl operator=(FieldsImpl&) = delete;
  FieldsImpl operator=(FieldsImpl&&) = delete;

  ara::com::proxy::MethodImpl<Data_t, void> Get;
};

template <typename Data_t>
class FieldsImpl<Data_t, true> final : public ara::com::proxy::Fields<Data_t> {
 private:
  /* data */
 public:
  FieldsImpl(const std::string& name,
             interpreter::ProxyPacketInterpreter& handler)  // NOLINT
      : ara::com::proxy::Fields<Data_t>{name, handler},
        Get{name + "/get", handler},
        Set{name + "/set", handler} {}
  ~FieldsImpl() = default;
  explicit FieldsImpl(FieldsImpl&&) = delete;
  explicit FieldsImpl(FieldsImpl&) = delete;
  FieldsImpl operator=(FieldsImpl&) = delete;
  FieldsImpl operator=(FieldsImpl&&) = delete;

  ara::com::proxy::MethodImpl<Data_t, void> Get;
  ara::com::proxy::MethodImpl<void, Data_t> Set;
};
}  // namespace proxy
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_PROXY_IMPL_FIELDS_H_
