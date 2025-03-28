/**
 * @file generic_data_identifier.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_DIAG_GENERIC_DATA_IDENTIFIER_H_
#define PLATFORM_DIAG_GENERIC_DATA_IDENTIFIER_H_

#include <vector>

#include "platform/diag/routing/routable_uds_service.h"
namespace platform {
namespace diag {
/// @brief A service to execute a specific sequence of instructions (routine)
/// and obtain the result
/// @note The class is not fully compatible with the platform standard.
class GenericDiD : public routing::RoutableUdsService {
 private:
  static const uint8_t cSid{0x22};

 public:
  /// @brief Constructor
  /// @pplatformm specifier Instance specifier that owns the service
  /// @pplatformm reentrancyType Service reentrancy type in a multi-threading contex
  explicit GenericDiD(const core::InstanceSpecifier &specifier);

  platform::core::Result<OperationOutput> HandleMessage(
      const std::vector<uint8_t> &requestData) override;

  virtual platform::core::Result<OperationOutput> Read() noexcept = 0;
  virtual platform::core::Result<void> Write(
      const std::vector<uint8_t> &) noexcept = 0;
};
}  // namespace diag
}  // namespace platform

#endif  // PLATFORM_DIAG_GENERIC_DATA_IDENTIFIER_H_
