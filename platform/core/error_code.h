/**
 * @file error_code.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_CORE_ERROR_CODE_H_
#define PLATFORM_CORE_ERROR_CODE_H_

#include "platform/core/error_domain.h"
#include "platform/core/string_view.h"

namespace platform {
namespace core {
class ErrorCode final {
 private:
  const ErrorDomain::CodeType value_;
  const ErrorDomain::SupportDataType data_;
  const ErrorDomain& domain_;

 public:
  template <typename EnumT>
  ErrorCode(EnumT e, ErrorDomain::SupportDataType data =
                                   ErrorDomain::SupportDataType()) noexcept
      : value_{e}, data_{data}, domain_{ErrorDomain{e}} {}

  ErrorCode(ErrorDomain::CodeType value, const ErrorDomain& domain,
                      ErrorDomain::SupportDataType data =
                          ErrorDomain::SupportDataType()) noexcept
      : value_{value}, data_{data}, domain_{domain} {}

  ErrorDomain::CodeType Value() const noexcept { return value_; }

  const ErrorDomain& Domain() const noexcept { return this->domain_; }
  ErrorDomain::SupportDataType SupportData() const noexcept {
    return this->data_;
  }
  StringView Message() const noexcept {
    return StringView{domain_.Message(value_)};
  }
  void ThrowAsException() const {}
  bool operator==(const ErrorCode& other) noexcept {
    return ((this->data_ == other.data_) && (this->value_ == other.value_));
  }
  bool operator!=(const ErrorCode& other) noexcept {
    return ((this->data_ != other.data_) || (this->value_ != other.value_));
  }
};
}  // namespace core
}  // namespace platform

#endif  // PLATFORM_CORE_ERROR_CODE_H_
