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
#ifndef ARA_EXEC_SM_I_EXECUTION_CLIENT_H_
#define ARA_EXEC_SM_I_EXECUTION_CLIENT_H_
#include "ara/core/result.h"
namespace ara {
namespace exec {
enum ExecutionState {
    kIdle = 0,
    kStarting = 1,
    kRunning = 2,
    kTerminating = 3,
    kTerminated = 4
};
class IExecutionClient {
 public:
  IExecutionClient() noexcept;

  ~IExecutionClient() noexcept;
  virtual ara::core::Result<void> ReportExecutionState(ExecutionState state) const noexcept = 0;
};
}  // namespace exec
}  // namespace ara

#endif  // ARA_EXEC_SM_I_EXECUTION_CLIENT_H_
