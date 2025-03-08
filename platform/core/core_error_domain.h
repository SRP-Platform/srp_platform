/**
 * @file core_error_domain.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_CORE_CORE_ERROR_DOMAIN_H_
#define PLATFORM_CORE_CORE_ERROR_DOMAIN_H_

#include <string>

#include "platform/core/error_code.h"
#include "platform/core/error_domain.h"

namespace platform {
namespace core {
enum class CoreErrc : ErrorDomain::CodeType {
  kInvalidArgument = 22,
  kInvalidMetaModelShortname = 137,
  kInvalidMetaModelPath = 138
};
class CoreErrorDomain final : public ErrorDomain {
 public:
  using Errc = CoreErrc;
  CoreErrorDomain() noexcept;
  const std::string Name() const noexcept override;
  const std::string Message(
      ErrorDomain::CodeType errorCode) const noexcept override;
};
ErrorCode MakeErrorCode(CoreErrc code,
                        ErrorDomain::SupportDataType data) noexcept;
constexpr const platform::core::ErrorDomain& GetCoreDomain() noexcept;
}  // namespace core
}  // namespace platform
#endif  // PLATFORM_CORE_CORE_ERROR_DOMAIN_H_
