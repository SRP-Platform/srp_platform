/**
 * @file com_error_domain.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/com/message_error_domain.h"

#include <unordered_map>

#include "platform/core/string_view.h"

namespace platform {
namespace com {
namespace internal {
namespace {
std::unordered_map<platform::core::ErrorDomain::CodeType,
                   platform::core::StringView>
    kKeyList{{static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kENotOk),
              "kENotOk"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEUnknownService),
              "kEUnknownService"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEUnknownMethod),
              "kEUnknownMethod"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kENotReady),
              "kENotReady"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kENotReachable),
              "kENotReachable"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kETimeout),
              "kETimeout"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEWrongProtocolVersion),
              "kEWrongProtocolVersion"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEWrongInterfaceVersion),
              "kEWrongInterfaceVersion"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEMalformedMessage),
              "kEMalformedMessage"},
             {static_cast<platform::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEGeneralError),
              "kEGeneralError"}};
const MessageErrorDomain domain_;
}  // namespace

MessageErrorDomain::MessageErrorDomain() noexcept
    : platform::core::ErrorDomain{0x8900000000001267} {}
const std::string MessageErrorDomain::Name() const noexcept {
  return platform::core::StringView{"MessageErrorDomain"};
}
const std::string MessageErrorDomain::Message(
    platform::core::ErrorDomain::CodeType errorCode) const noexcept {
  return kKeyList[errorCode];
}

platform::core::ErrorCode MakeErrorCode(
    MessageErrorDomain::Errc code,
    platform::core::ErrorDomain::SupportDataType data) noexcept {
  return platform::core::ErrorCode{
      static_cast<platform::core::ErrorDomain::CodeType>(code), GetComDomain(),
      data};
}

const platform::core::ErrorDomain& GetComDomain() noexcept { return domain_; }
}  // namespace internal
}  // namespace com
}  // namespace platform
