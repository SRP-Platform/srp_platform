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
#include "ara/com/message_error_domain.h"

#include <unordered_map>

#include "ara/core/string_view.h"

namespace ara {
namespace com {
namespace internal {
namespace {
std::unordered_map<ara::core::ErrorDomain::CodeType, ara::core::StringView>
    kKeyList{{static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kENotOk),
              "kENotOk"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEUnknownService),
              "kEUnknownService"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEUnknownMethod),
              "kEUnknownMethod"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kENotReady),
              "kENotReady"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kENotReachable),
              "kENotReachable"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kETimeout),
              "kETimeout"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEWrongProtocolVersion),
              "kEWrongProtocolVersion"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEWrongInterfaceVersion),
              "kEWrongInterfaceVersion"},
             {static_cast<ara::core::ErrorDomain::CodeType>(
                  MessageErrorDomain::Errc::kEMalformedMessage),
              "kEMalformedMessage"}};
const MessageErrorDomain domain_;
}  // namespace

MessageErrorDomain::MessageErrorDomain() noexcept
    : ara::core::ErrorDomain{0x8900000000001267} {}
const std::string MessageErrorDomain::Name() const noexcept {
  return ara::core::StringView{"MessageErrorDomain"};
}
const std::string MessageErrorDomain::Message(
    ara::core::ErrorDomain::CodeType errorCode) const noexcept {
  return kKeyList[errorCode];
}

ara::core::ErrorCode MakeErrorCode(
    MessageErrorDomain::Errc code,
    ara::core::ErrorDomain::SupportDataType data) noexcept {
  return ara::core::ErrorCode{
      static_cast<ara::core::ErrorDomain::CodeType>(code), GetComDomain(),
      data};
}

const ara::core::ErrorDomain& GetComDomain() noexcept { return domain_; }
}  // namespace internal
}  // namespace com
}  // namespace ara
