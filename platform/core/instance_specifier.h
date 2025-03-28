/**
 * @file instance_specifier.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-08-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_CORE_INSTANCE_SPECIFIER_H_
#define PLATFORM_CORE_INSTANCE_SPECIFIER_H_
#include "platform/core/result.h"
#include "platform/core/string_view.h"
namespace platform {
namespace core {
class InstanceSpecifier final {
 private:
  StringView metaModelIdentifier_;

 public:
  explicit InstanceSpecifier(StringView metaModelIdentifier);
  InstanceSpecifier(const InstanceSpecifier &other);
  InstanceSpecifier(InstanceSpecifier &&other) noexcept;
  InstanceSpecifier &operator=(const InstanceSpecifier &other);
  InstanceSpecifier &operator=(InstanceSpecifier &&other);
  ~InstanceSpecifier() noexcept;
  static Result<InstanceSpecifier> Create(
      StringView metaModelIdentifier) noexcept;
  bool operator==(const InstanceSpecifier &other) const noexcept;
  bool operator==(StringView other) const noexcept;
  bool operator!=(const InstanceSpecifier &other) const noexcept;
  bool operator!=(StringView other) const noexcept;
  bool operator<(const InstanceSpecifier &other) const noexcept;
  StringView ToString() const noexcept;
};
}  // namespace core
}  // namespace platform

#endif  // PLATFORM_CORE_INSTANCE_SPECIFIER_H_
