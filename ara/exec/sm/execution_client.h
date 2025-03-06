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
#include "ara/exec/sm/i_execution_client.h"
#include "bindings/common/socket/ipc_socket.h"
namespace ara {
namespace exec {
using ISocket = srp::bindings::com::soc::ISocket;
using IpcSocket = srp::bindings::com::soc::IpcSocket;
class ExecutionClient: public IExecutionClient {
 private:
  const uint32_t app_id_;
  std::unique_ptr<ISocket> sock_;
 public:
  ara::core::Result<void> ReportExecutionState(ExecutionState state) const noexcept override;
  explicit ExecutionClient(const uint32_t& app_id, std::unique_ptr<ISocket> sock
                                                = std::make_unique<IpcSocket>());
  ~ExecutionClient() = default;
};
}
}

#endif  // ARA_EXEC_SM_EXECUTION_CLIENT_H_
