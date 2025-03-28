/**
 * @file method.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-02-16
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef PLATFORM_COM_PROXY_IMPL_METHOD_H_
#define PLATFORM_COM_PROXY_IMPL_METHOD_H_

#include <memory>
#include <string>
#include <vector>

#include "platform/com/proxy/method.h"
#include "platform/core/result.h"
#include "core/data/type_converter.h"

namespace platform {
namespace com {
namespace proxy {
template <typename Output_t, typename Input_t>
class MethodImpl final : public platform::com::proxy::Method {
 public:
  using ReturnType = Output_t;
  using InputType = Input_t;

 private:
  std::unique_ptr<platform::core::Result<ReturnType>> value{nullptr};
  void ProceedAck() noexcept {}
  void ProceedResponse(const std::vector<uint8_t>& payload) noexcept override {
    const auto temp = srp::data::Convert<ReturnType>::Conv(payload);
    if (temp.has_value()) {
      value = std::make_unique<platform::core::Result<ReturnType>>(temp.value());
    } else {
      value = std::make_unique<platform::core::Result<ReturnType>>(
          internal::MakeErrorCode(
              internal::MessageErrorDomain::Errc::kEMalformedMessage, ""));
    }
  }
  void ProceedError(const uint8_t error_code) noexcept override {
    value =
        std::make_unique<platform::core::Result<ReturnType>>(internal::MakeErrorCode(
            static_cast<internal::MessageErrorDomain::Errc>(error_code), ""));
  }

 public:
  MethodImpl(const std::string& name,
             interpreter::ProxyPacketInterpreter& handler)  // NOLINT
      : platform::com::proxy::Method(name, handler) {}

  ~MethodImpl() = default;

  MethodImpl(const MethodImpl&) = delete;
  MethodImpl(MethodImpl&&) = delete;
  MethodImpl* operator=(const MethodImpl&) = delete;
  MethodImpl* operator=(const MethodImpl&&) = delete;

  platform::core::Result<ReturnType> operator()(const InputType& val) noexcept {
    this->SendRequest(srp::data::Convert2Vector<InputType>::Conv(val));

    return *value.get();
  }
};

template <typename Output_t>
class MethodImpl<Output_t, void> final : public platform::com::proxy::Method {
 public:
  using ReturnType = Output_t;
  using InputType = void;

 private:
  std::unique_ptr<platform::core::Result<ReturnType>> value{nullptr};
  void ProceedAck() noexcept {}
  void ProceedResponse(const std::vector<uint8_t>& payload) noexcept override {
    const auto temp = srp::data::Convert<ReturnType>::Conv(payload);
    if (temp.has_value()) {
      value = std::make_unique<platform::core::Result<ReturnType>>(temp.value());
    } else {
      value = std::make_unique<platform::core::Result<ReturnType>>(
          internal::MakeErrorCode(
              internal::MessageErrorDomain::Errc::kEMalformedMessage, ""));
    }
  }
  void ProceedError(const uint8_t error_code) noexcept override {
    value =
        std::make_unique<platform::core::Result<ReturnType>>(internal::MakeErrorCode(
            static_cast<internal::MessageErrorDomain::Errc>(error_code), ""));
  }

 public:
  MethodImpl(const std::string& name,
             interpreter::ProxyPacketInterpreter& handler)  // NOLINT
      : platform::com::proxy::Method(name, handler) {}

  ~MethodImpl() noexcept = default;

  MethodImpl(const MethodImpl&) = delete;
  MethodImpl(MethodImpl&&) = delete;
  MethodImpl* operator=(const MethodImpl&) = delete;
  MethodImpl* operator=(const MethodImpl&&) = delete;

  platform::core::Result<ReturnType> operator()() noexcept {
    this->SendRequest({});

    return *value.get();
  }
};

template <typename Input_t>
class MethodImpl<void, Input_t> final : public platform::com::proxy::Method {
 public:
  using InputType = Input_t;
  using ReturnType = void;

 private:
  std::unique_ptr<platform::core::Result<ReturnType>> value{nullptr};
  void ProceedAck() noexcept {
    value = std::make_unique<platform::core::Result<ReturnType>>();
  }
  void ProceedResponse(const std::vector<uint8_t>& payload) noexcept override {
    value = std::make_unique<platform::core::Result<ReturnType>>();
  }
  void ProceedError(const uint8_t error_code) noexcept override {
    value =
        std::make_unique<platform::core::Result<ReturnType>>(internal::MakeErrorCode(
            static_cast<internal::MessageErrorDomain::Errc>(error_code), ""));
  }

 public:
  MethodImpl(const std::string& name,
             interpreter::ProxyPacketInterpreter& handler)  // NOLINT
      : platform::com::proxy::Method(name, handler) {}

  ~MethodImpl() noexcept = default;

  MethodImpl(const MethodImpl&) = delete;
  MethodImpl(MethodImpl&&) = delete;
  MethodImpl* operator=(const MethodImpl&) = delete;
  MethodImpl* operator=(const MethodImpl&&) = delete;

  platform::core::Result<ReturnType> operator()(const InputType& val) noexcept {
    this->SendRequest(srp::data::Convert2Vector<InputType>::Conv(val));

    return *value.get();
  }
};

template <>
class MethodImpl<void, void> final : public platform::com::proxy::Method {
 public:
  using InputType = void;
  using ReturnType = void;

 private:
  std::unique_ptr<platform::core::Result<ReturnType>> value{nullptr};
  void ProceedAck() noexcept {
    value = std::make_unique<platform::core::Result<ReturnType>>();
  }
  void ProceedResponse(const std::vector<uint8_t>& payload) noexcept override {
    value = std::make_unique<platform::core::Result<ReturnType>>();
  }
  void ProceedError(const uint8_t error_code) noexcept override {
    value =
        std::make_unique<platform::core::Result<ReturnType>>(internal::MakeErrorCode(
            static_cast<internal::MessageErrorDomain::Errc>(error_code), ""));
  }

 public:
  MethodImpl(const std::string& name,
             interpreter::ProxyPacketInterpreter& handler)  // NOLINT
      : platform::com::proxy::Method(name, handler) {}

  ~MethodImpl() noexcept = default;

  MethodImpl(const MethodImpl&) = delete;
  MethodImpl(MethodImpl&&) = delete;
  MethodImpl* operator=(const MethodImpl&) = delete;
  MethodImpl* operator=(const MethodImpl&&) = delete;

  platform::core::Result<ReturnType> operator()() noexcept {
    this->SendRequest({});

    return *value.get();
  }
};

}  // namespace proxy
}  // namespace com
}  // namespace platform

#endif  // PLATFORM_COM_PROXY_IMPL_METHOD_H_
