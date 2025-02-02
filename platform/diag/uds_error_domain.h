/**
 * @file UDS_error_domain.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_DIAG_UDS_ERROR_DOMAIN_H_
#define PLATFORM_DIAG_UDS_ERROR_DOMAIN_H_

#include <string>

#include "platform/core/error_code.h"
#include "platform/core/error_domain.h"

namespace platform {
namespace diag {
enum UdsDiagErrc : platform::core::ErrorDomain::CodeType {
  kGeneralReject = 0x10,
  kSubFunctionNotSupported = 0x12,
  kInvalidMessageLengthFormat = 0x13,
  kConditionsNotCorrect = 0x22,
  kRequestSequenceError = 0x24,
  kFailurePreventsExecutionOfRequestedAction = 0x26,
  kRequestOutOfRange = 0x31,
  kSecurityAccessDenied = 0x33,
  kInvalidKey = 0x35,
  kExceededNumberOfAttempts = 0x36,
  kRequiredTimeDelayHasNotExpired = 0x37,
  kUploadDownloadNotAccepted = 0x70,
  kTransferDataSuspended = 0x71,
  kProgrammingFailure = 0x72,
  kWrongBlockSequenceCounter = 0x73,
};

class UdsErrorDomain final : public platform::core::ErrorDomain {
 public:
  using Errc = UdsDiagErrc;

  UdsErrorDomain() noexcept;
  const std::string Name() const noexcept override;
  const std::string Message(
      platform::core::ErrorDomain::CodeType errorCode) const noexcept override;
};

platform::core::ErrorCode MakeErrorCode(
    UdsDiagErrc code, platform::core::ErrorDomain::SupportDataType data) noexcept;

platform::core::ErrorCode MakeErrorCode(UdsDiagErrc code) noexcept;

constexpr const platform::core::ErrorDomain& GetDiagDomain() noexcept;
}  // namespace diag
}  // namespace platform
#endif  // PLATFORM_DIAG_UDS_ERROR_DOMAIN_H_
