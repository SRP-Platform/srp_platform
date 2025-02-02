/**
 * @file generic_routine.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-11-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_DIAG_GENERIC_ROUTINE_H_
#define PLATFORM_DIAG_GENERIC_ROUTINE_H_

#include <vector>

#include "platform/diag/routing/routable_uds_service.h"
namespace platform {
namespace diag {
/// @brief A service to execute a specific sequence of instructions (routine)
/// and obtain the result
/// @note The class is not fully compatible with the platform standard.
class GenericRoutine : public routing::RoutableUdsService {
 private:
  static const uint8_t cSid{0x31};

 public:
  /// @brief Constructor
  /// @pplatformm specifier Instance specifier that owns the service
  /// @pplatformm reentrancyType Service reentrancy type in a multi-threading contex
  explicit GenericRoutine(const core::InstanceSpecifier &specifier);

  platform::core::Result<OperationOutput> HandleMessage(
      const std::vector<uint8_t> &requestData) override;

  /// @brief Request starting a routine
  /// @pplatformm requestData Start request byte array
  virtual platform::core::Result<OperationOutput> Start(
      std::vector<uint8_t> requestData) = 0;

  /// @brief Request stopping a routine
  /// @pplatformm requestData Stop request byte array
  virtual platform::core::Result<OperationOutput> Stop(
      std::vector<uint8_t> requestData) = 0;

  /// @brief Request the results of a routine
  /// @pplatformm requestData Results request byte array
  virtual platform::core::Result<OperationOutput> RequestResults(
      std::vector<uint8_t> requestData) = 0;
};
}  // namespace diag
}  // namespace platform

#endif  // PLATFORM_DIAG_GENERIC_ROUTINE_H_
