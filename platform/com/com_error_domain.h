/**
 * @file com_error_domain.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COM_COM_ERROR_DOMAIN_H_
#define PLATFORM_COM_COM_ERROR_DOMAIN_H_
#include <string>

#include "platform/core/error_code.h"
#include "platform/core/error_domain.h"

namespace platform {
namespace com {
enum class ComErrc : platform::core::ErrorDomain::CodeType {
  kServiceNotAvailable = 1,
  kMaxSamplesReached = 2,
  kNetworkBindingFailure = 3,
  kGrantEnforcementError = 4,
  kPeerIsUnreachable = 5,
  kFieldValueIsNotValid = 6,
  kSetHandlerNotSet = 7,
  kUnsetFailure = 8,
  kSampleAllocationFailure = 9,
  kIllegalUseOfAllocate = 10,
  kServiceNotOffered = 11,
  kCommunicationLinkError = 12,
  kNoClients = 13,
  kCommunicationStackError = 14,
  kInstanceIDCouldNotBeResolved = 15,
  kMaxSampleCountNotRealizable = 16,
  kWrongMethodCallProcessingMode = 17,
  kErroneousFileHandle = 18
};

enum class ComOfferErrc : platform::core::ErrorDomain::CodeType {
  kAlreadyOffered = 101,
  kConfigurationMismatch = 102
};

class ComErrorDomain final : public platform::core::ErrorDomain {
 public:
  using Errc = ComErrc;

  ComErrorDomain() noexcept;
  const std::string Name() const noexcept override;
  const std::string Message(
      platform::core::ErrorDomain::CodeType errorCode) const noexcept override;
};

platform::core::ErrorCode MakeErrorCode(
    ComErrc code, platform::core::ErrorDomain::SupportDataType data) noexcept;

const platform::core::ErrorDomain& GetComDomain() noexcept;
}  // namespace com
}  // namespace platform
#endif  // PLATFORM_COM_COM_ERROR_DOMAIN_H_
