/**
 * @file transport_error.cc
 * @copyright Copyright (c) 2024
 */
#include "ara/com/mavlink/transport_error.h"

#include <string>

namespace ara {
namespace com {
namespace mavlink {
namespace {
class TransportErrorDomain final : public ara::core::ErrorDomain {
 public:
  TransportErrorDomain() noexcept : ara::core::ErrorDomain{0x8000000000002A01} {}
  const std::string Name() const noexcept override {
    return std::string{"MavlinkTransportErrorDomain"};
  }
  const std::string Message(CodeType code) const noexcept override {
    switch (static_cast<TransportErrc>(code)) {
      case TransportErrc::kRetryable: return "Retryable transport error";
      case TransportErrc::kPersistent: return "Persistent transport error";
      case TransportErrc::kClosed: return "Transport closed";
      case TransportErrc::kUnknownLink: return "Unknown link";
      case TransportErrc::kInvalidArgument: return "Invalid argument";
      case TransportErrc::kAlreadyRegistered: return "Transport already registered";
      case TransportErrc::kBusy: return "Radio busy";
      case TransportErrc::kPluginError: return "Radio driver plugin error";
    }
    return "Unknown transport error";
  }
};
const TransportErrorDomain kDomain;
}  // namespace

const ara::core::ErrorDomain& GetTransportErrorDomain() noexcept {
  return kDomain;
}

ara::core::ErrorCode MakeTransportError(
    TransportErrc code, ara::core::ErrorDomain::SupportDataType data) {
  return ara::core::ErrorCode{
      static_cast<ara::core::ErrorDomain::CodeType>(code), kDomain, data};
}

bool IsTransportError(const ara::core::ErrorCode& e, TransportErrc code) noexcept {
  return e.Domain() == kDomain &&
         e.Value() == static_cast<ara::core::ErrorDomain::CodeType>(code);
}

}  // namespace mavlink
}  // namespace com
}  // namespace ara
