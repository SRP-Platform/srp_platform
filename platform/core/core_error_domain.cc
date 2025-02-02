/**
 * @file core_error_domain.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-11
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/core/core_error_domain.h"

#include <string>
#include <unordered_map>

#include "platform/core/string_view.h"
namespace platform {
namespace core {
namespace {
std::unordered_map<ErrorDomain::CodeType, StringView> kKeyList{
    {static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidArgument),
     "Invalid Argument"},
    {static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelShortname),
     "Invalid Meta Model Shortname"},
    {static_cast<ErrorDomain::CodeType>(CoreErrc::kInvalidMetaModelPath),
     "Invalid Meta Model Path"}};
const CoreErrorDomain domain_;
}  // namespace

CoreErrorDomain::CoreErrorDomain() noexcept : ErrorDomain{0x01} {}
const std::string CoreErrorDomain::Name() const noexcept {
  return std::string{"CoreErrorDomain"};
}
const std::string CoreErrorDomain::Message(
    ErrorDomain::CodeType errorCode) const noexcept {
  return kKeyList[errorCode];
}
ErrorCode MakeErrorCode(CoreErrc code,
                        ErrorDomain::SupportDataType data) noexcept {
  return ErrorCode{static_cast<ErrorDomain::CodeType>(code), GetCoreDomain(),
                   data};
}

constexpr const platform::core::ErrorDomain& GetCoreDomain() noexcept {
  return domain_;
}

}  // namespace core
}  // namespace platform
