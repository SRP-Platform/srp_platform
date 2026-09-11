/**
 * @file em_service.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-06
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COMMON_EM_CODE_SERVICES_EM_EM_SERVICE_H_
#define PLATFORM_COMMON_EM_CODE_SERVICES_EM_EM_SERVICE_H_
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <unordered_set>
#include <vector>

#include "ara/exec/em/i_execution_client.h"
#include "bindings/common/socket/proccess_socket.h"
#include "platform/common/em/code/services/em/app_config.h"
#include "platform/common/em/code/services/em/i_app_db.h"

namespace srp {
namespace em {
namespace service {
class EmService {
 private:
  std::unordered_set<uint16_t> current_fg_apps;
  uint16_t active_state{0U};
  const std::shared_ptr<data::IAppDb> db_;
  const std::function<void(const uint16_t&)> update_callback_;
  srp::bindings::com::ProccessSocket proc_sock_;
  const int start_timeout_ms_;
  const int stop_timeout_ms_;
  bool ipc_offered_{false};
  std::atomic<bool> running_{true};
  std::mutex service_mtx_;
  bool IsSrpApp(const std::string& path) noexcept;
  void ProcessSockCallback(const uint32_t pid,
                           const std::vector<uint8_t>& buf) noexcept;
  pid_t StartApp(const srp::em::service::data::AppConfig& app);
  void KillApp(const pid_t pid, bool force = false);
  void KillApps(const std::vector<uint16_t>& terminate_list);
  void ReapPid(const pid_t pid);

 public:
  uint16_t GetActiveState() const { return active_state; }
  bool IsIpcOffered() const { return ipc_offered_; }
  void HandleExecFrame(const std::vector<uint8_t>& buf) noexcept;
  bool WaitForAppStatus(const uint16_t& app_id_,
                        const ara::exec::ExecutionState state, int timeout_ms);
  void LoadApps() noexcept;
  void SetActiveState(const uint16_t& state_id_) noexcept;
  std::optional<pid_t> RestartApp(const uint16_t appID);
  EmService(std::shared_ptr<data::IAppDb> db,
            const std::function<void(const uint16_t&)>&& update_callback,
            int start_timeout_ms = 5000, int stop_timeout_ms = 5000);
  ~EmService();
};

}  // namespace service
}  // namespace em
}  // namespace srp
#endif  // PLATFORM_COMMON_EM_CODE_SERVICES_EM_EM_SERVICE_H_
