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

#ifndef PLATFORM_EXEC_EM_EXECUTION_CLIENT_H_
#define PLATFORM_EXEC_EM_EXECUTION_CLIENT_H_
#include <memory>
#include <vector>
#include "platform/exec/em/i_execution_client.h"
#include "bindings/common/controller/controller_client.h"
namespace platform {
namespace exec {
class ExecutionClient: public srp::bindings::ControllerClient, public IExecutionClient {
 private:
    uint32_t app_id_;
 public:
    platform::core::Result<void> ReportExecutionState(const ExecutionState state) const noexcept override;
    ExecutionClient();
    ~ExecutionClient() = default;
    ExecutionClient(const ExecutionClient&) = delete;
    ExecutionClient& operator=(const ExecutionClient&) = delete;
    ExecutionClient(ExecutionClient&&) = delete;
    ExecutionClient operator=(ExecutionClient&&) = delete;
    static std::shared_ptr<ExecutionClient> GetInstance(const uint32_t& app_id) noexcept;
    static std::shared_ptr<ExecutionClient> GetInstance() noexcept;
    void HandleNewMsg(uint32_t pid,
        const std::vector<uint8_t>& payload) noexcept override;
    void Init() noexcept override {};
};
}  // namespace exec
}  // namespace platform

#endif  // PLATFORM_EXEC_EM_EXECUTION_CLIENT_H_
