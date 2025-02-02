/**
 * @file diag_error_domain.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/diag/uds_error_domain.h"

#include <string>
#include <unordered_map>

#include "platform/core/string_view.h"

namespace platform {
namespace diag {
namespace {
std::unordered_map<platform::core::ErrorDomain::CodeType, platform::core::StringView>
    kKeyList{
        {UdsDiagErrc::kGeneralReject, "kGeneralReject"},
        {UdsDiagErrc::kSubFunctionNotSupported, "kSubFunctionNotSupported"},
        {UdsDiagErrc::kInvalidMessageLengthFormat,
         "kInvalidMessageLengthFormat"},
        {UdsDiagErrc::kConditionsNotCorrect, "kConditionsNotCorrect"},
        {UdsDiagErrc::kRequestSequenceError, "kRequestSequenceError"},
        {UdsDiagErrc::kFailurePreventsExecutionOfRequestedAction,
         "kFailurePreventsExecutionOfRequestedAction"},
        {UdsDiagErrc::kRequestOutOfRange, "kRequestOutOfRange"},
        {UdsDiagErrc::kSecurityAccessDenied, "kSecurityAccessDenied"},
        {UdsDiagErrc::kInvalidKey, "kInvalidKey"},
        {UdsDiagErrc::kExceededNumberOfAttempts, "kExceededNumberOfAttempts"},
        {UdsDiagErrc::kRequiredTimeDelayHasNotExpired,
         "kRequiredTimeDelayHasNotExpired"},
        {UdsDiagErrc::kUploadDownloadNotAccepted, "kUploadDownloadNotAccepted"},
        {UdsDiagErrc::kTransferDataSuspended, "kTransferDataSuspended"},
        {UdsDiagErrc::kProgrammingFailure, "kProgrammingFailure"},
        {UdsDiagErrc::kWrongBlockSequenceCounter, "kWrongBlockSequenceCounter"},
    };
const UdsErrorDomain domain_;
}  // namespace

UdsErrorDomain::UdsErrorDomain() noexcept
    : platform::core::ErrorDomain{0x8000000000000404} {}
const std::string UdsErrorDomain::Name() const noexcept {
  return std::string{"CoreErrorDomain"};
}
const std::string UdsErrorDomain::Message(
    platform::core::ErrorDomain::CodeType errorCode) const noexcept {
  return kKeyList[errorCode];
}

platform::core::ErrorCode MakeErrorCode(
    UdsDiagErrc code, platform::core::ErrorDomain::SupportDataType data) noexcept {
  return platform::core::ErrorCode{
      static_cast<platform::core::ErrorDomain::CodeType>(code), GetDiagDomain(),
      data};
}

platform::core::ErrorCode MakeErrorCode(UdsDiagErrc code) noexcept {
  return MakeErrorCode(code, "");
}

constexpr const platform::core::ErrorDomain& GetDiagDomain() noexcept {
  return domain_;
}
}  // namespace diag
}  // namespace platform
