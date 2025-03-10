/**
 * @file i_execution_client.h
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-02-26
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef PLATFORM_EXEC_EM_I_EXECUTION_CLIENT_H_
#define PLATFORM_EXEC_EM_I_EXECUTION_CLIENT_H_
#include "platform/core/result.h"
namespace platform {
namespace exec {
enum ExecutionState: uint8_t {
    kIdle = 0U,
    kStarting = 1U,
    kRunning = 2U,
    kTerminating = 3U,
    kTerminated = 4U
};
class IExecutionClient {
 public:
  IExecutionClient() noexcept;

  ~IExecutionClient() noexcept;
  virtual platform::core::Result<void> ReportExecutionState(const ExecutionState state) const noexcept = 0;
};
}  // namespace exec
}  // namespace platform

#endif  // PLATFORM_EXEC_EM_I_EXECUTION_CLIENT_H_
