/**
 * @file app_config.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-06
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_EM_CODE_SERVICES_EM_APP_CONFIG_H_
#define APPS_COMMON_EM_CODE_SERVICES_EM_APP_CONFIG_H_
#include <cstdint>
#include <string>
#include <vector>
#include "platform/exec/em/i_execution_client.h"
namespace srp {
namespace em {
namespace service {
namespace data {
class AppConfig {
 private:
  const std::string bin_path_;
  const std::string parms_;
  const uint16_t app_id_;
  const std::string app_name_;
  const std::vector<std::string> fg_list_;
  pid_t pid{0};
  platform::exec::ExecutionState state_{platform::exec::ExecutionState::kIdle};

 public:
  AppConfig(const std::string& bin_path, const std::string& parms,
            const uint16_t& app_id, const std::string& app_name,
            const std::vector<std::string>& fg_list)
      : bin_path_{bin_path},
        parms_{parms},
        app_id_{app_id},
        app_name_{app_name},
        fg_list_{fg_list} {}

  std::string GetBinPath() const { return bin_path_; }
  std::string GetParms() const { return parms_; }
  const std::string& GetAppName() const { return app_name_; }
  const uint16_t GetAppId() const { return app_id_; }
  const std::vector<std::string>& GetFg() const { return fg_list_; }
  pid_t GetPid() const { return pid; }
  void SetPid(pid_t pid) { this->pid = pid; }
  const platform::exec::ExecutionState GetExecutionState() const { return state_; }
  void SetExecutionState(platform::exec::ExecutionState state) { this->state_ = state; }
};
}  // namespace data
}  // namespace service
}  // namespace em
}  // namespace srp

#endif  // APPS_COMMON_EM_CODE_SERVICES_EM_APP_CONFIG_H_
