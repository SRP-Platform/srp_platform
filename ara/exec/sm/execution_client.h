/**
 * @file execution_client.h
 * @author Mateusz Krajewski (matikrajek42@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-02-26
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef ARA_EXEC_SM_EXECUTION_CLIENT_H_
#define ARA_EXEC_SM_EXECUTION_CLIENT_H_
#include <memory>
#include <vector>
#include "ara/exec/sm/i_execution_client.h"
#include "ara/com/i_com_client.h"
namespace ara {
namespace exec {
class ExecutionClient: public com::IComClient, public IExecutionClient {
 private:
 public:
  ara::core::Result<void> ReportExecutionState(ExecutionState state) const noexcept override;
  ExecutionClient();
  ~ExecutionClient() = default;
  static std::shared_ptr<ExecutionClient> GetInstance() noexcept;
  void HandleNewMsg(uint32_t pid,
    const std::vector<uint8_t>& payload) noexcept override;
};
}
}

#endif  // ARA_EXEC_SM_EXECUTION_CLIENT_H_
