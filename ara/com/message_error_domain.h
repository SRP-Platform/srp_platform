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
#ifndef ARA_COM_MESSAGE_ERROR_DOMAIN_H_
#define ARA_COM_MESSAGE_ERROR_DOMAIN_H_
#include <string>

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
namespace ara {
namespace com {
namespace internal {
enum MessageErrorCode : uint8_t {
  kEOk = 0x00U,
  kENotOk = 0x01U,
  kEUnknownService = 0x02U,
  kEUnknownMethod = 0x03U,
  kENotReady = 0x04U,
  kENotReachable = 0x05U,
  kETimeout = 0x06U,
  kEWrongProtocolVersion = 0x07U,
  kEWrongInterfaceVersion = 0x08U,
  kEMalformedMessage = 0x09U,
  kEGeneralError = 0xFFU,
};

class MessageErrorDomain final : public ara::core::ErrorDomain {
 public:
  using Errc = MessageErrorCode;

  MessageErrorDomain() noexcept;
  const std::string Name() const noexcept override;
  const std::string Message(
      ara::core::ErrorDomain::CodeType errorCode) const noexcept override;
};

ara::core::ErrorCode MakeErrorCode(
    MessageErrorDomain::Errc code,
    ara::core::ErrorDomain::SupportDataType data) noexcept;
const ara::core::ErrorDomain& GetComDomain() noexcept;
}  // namespace internal
}  // namespace com
}  // namespace ara
#endif  // ARA_COM_MESSAGE_ERROR_DOMAIN_H_
