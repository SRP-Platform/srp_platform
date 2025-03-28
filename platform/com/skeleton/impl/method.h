/**
 * @file method.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2025-03-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef PLATFORM_COM_SKELETON_IMPL_METHOD_H_
#define PLATFORM_COM_SKELETON_IMPL_METHOD_H_

#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/data/type_converter.h"
#include "platform/com/skeleton/method.h"

namespace platform {
namespace com {
namespace skeleton {
template <typename Output_t, typename Input_t>
class MethodImpl final : public platform::com::skeleton::Method {
 public:
  using MethodType =
      std::function<platform::core::Result<Output_t>(const Input_t&)>;

 protected:
  platform::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t>& payload) noexcept override {
    if (!method_) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEGeneralError,
          "Method not implemented");
    }
    const auto input_data = srp::data::Convert<Input_t>::Conv(payload);
    if (!input_data.has_value()) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEMalformedMessage,
          "Incorrect payload format");
    }
    const auto res = method_(input_data.value());
    if (!res.HasValue()) {
      return platform::core::Result<std::vector<uint8_t>>{res.Error()};
    }
    return srp::data::Convert2Vector<Output_t>::Conv(res.Value());
  }
  MethodType method_;

 public:
  MethodImpl(
      const std::string& endpoint_name,
      interpreter::SkeletonPacketInterpreter& handler) noexcept  // NOLINT
      : Method{endpoint_name, handler}, method_{} {}
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler,  // NOLINT
             MethodType&& method)
      : Method{endpoint_name, handler}, method_{std::move(method)} {}

  void Bind(MethodType&& method) { this->method_ = std::move(method); }
};

template <typename Output_t>
class MethodImpl<Output_t, void> final : public Method {
 public:
  using MethodType = std::function<platform::core::Result<Output_t>()>;

 protected:
  platform::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t>& payload) noexcept override {
    if (!method_) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEGeneralError,
          "Method not implemented");
    }

    if (0U != payload.size()) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEMalformedMessage,
          "Incorrect payload format");
    }
    const auto res = method_();
    if (!res.HasValue()) {
      return platform::core::Result<std::vector<uint8_t>>{res.Error()};
    }
    return srp::data::Convert2Vector<Output_t>::Conv(res.Value());
  }
  MethodType method_;

 public:
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler)  // NOLINT
      : Method{endpoint_name, handler}, method_{} {}
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler,
             MethodType&& method)
      : Method{endpoint_name, handler}, method_{std::move(method)} {}

  void Bind(MethodType&& method) { this->method_ = std::move(method); }
};

template <typename Input_t>
class MethodImpl<void, Input_t> final : public Method {
 public:
  using MethodType =
      std::function<platform::core::Result<void>(const Input_t&)>;

 protected:
  platform::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t>& payload) noexcept override {
    if (!method_) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEGeneralError,
          "Method not implemented");
    }
    const auto input_data = srp::data::Convert<Input_t>::Conv(payload);
    if (!input_data.has_value()) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEMalformedMessage,
          "Incorrect payload format");
    }

    const auto res = method_(input_data.value());
    if (!res.HasValue()) {
      return platform::core::Result<std::vector<uint8_t>>{res.Error()};
    }
    return std::vector<uint8_t>{};
  }
  MethodType method_;

 public:
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler)  // NOLINT
      : Method{endpoint_name, handler}, method_{} {}
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler,  // NOLINT
             MethodType&& method)
      : Method{endpoint_name, handler}, method_{std::move(method)} {}

  void Bind(MethodType&& method) { this->method_ = std::move(method); }
};

template <>
class MethodImpl<void, void> final : public Method {
 public:
  using MethodType = std::function<platform::core::Result<void>()>;

 protected:
  platform::core::Result<std::vector<uint8_t>> ProceedRequest(
      const std::vector<uint8_t>& payload) noexcept override {
    if (!method_) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEGeneralError,
          "Method not implemented");
    }

    if (0U == payload.size()) {
      return platform::com::internal::MakeErrorCode(
          platform::com::internal::MessageErrorCode::kEMalformedMessage,
          "Incorrect payload format");
    }

    const auto res = method_();
    if (!res.HasValue()) {
      return platform::core::Result<std::vector<uint8_t>>{res.Error()};
    }
    return std::vector<uint8_t>{};
  }
  MethodType method_;

 public:
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler)  // NOLINT
      : Method{endpoint_name, handler}, method_{} {}
  MethodImpl(const std::string& endpoint_name,
             interpreter::SkeletonPacketInterpreter& handler,  // NOLINT
             MethodType&& method)
      : Method{endpoint_name, handler}, method_{std::move(method)} {}

  void Bind(MethodType&& method) { this->method_ = std::move(method); }
};

}  // namespace skeleton
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_SKELETON_IMPL_METHOD_H_
