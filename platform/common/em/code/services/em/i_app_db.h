/**
 * @file i_app_db.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-12-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_COMMON_EM_CODE_SERVICES_EM_I_APP_DB_H_
#define PLATFORM_COMMON_EM_CODE_SERVICES_EM_I_APP_DB_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

#include "ara/exec/em/i_execution_client.h"
#include "platform/common/em/code/services/em/app_config.h"
namespace srp {
namespace em {
namespace service {
namespace data {
class IAppDb {
 public:
  /**
   * @brief Adding new App config to DB
   *
   * @param app New Config
   * @return int8_t 0 = OK
   */
  virtual int8_t InsertNewApp(AppConfig app) noexcept = 0;
  /**
   * @brief Adding new Functional group to DB
   *
   * @param fg_id functional group id
   * @param name functional group name
   * @return int8_t 0 = OK
   */
  virtual int8_t InsertNewFG(uint16_t fg_id,
                             const std::string& name) noexcept = 0;
  /**
   * @brief Set the Pid For App object
   *
   * @param app_id App ID from config file
   * @param pid new PID
   */
  virtual void SetPidForApp(const uint16_t app_id,
                            const uint32_t pid) noexcept = 0;
  /**
   * @brief Get the Pid For App object
   *
   * @param app_id App ID from config file
   * @return pid_t stored PID, 0 if app is unknown or not started
   */
  virtual pid_t GetPidForApp(const uint16_t app_id) noexcept = 0;
  /**
   * @brief Get the App Config object
   *
   * @param app_id_ App ID for which we want config
   * @return std::optional<AppConfig> snapshot of AppConfig
   */
  virtual std::optional<AppConfig> GetAppConfig(
      const uint16_t& app_id_) noexcept = 0;
  /**
   * @brief Get the Fg App List object
   *
   * @param fg_id functional group id
   * @return std::optional<std::unordered_set<uint16_t>> copy of app ids in FG
   */
  virtual std::optional<std::unordered_set<uint16_t>> GetFgAppList(
      const uint16_t& fg_id) noexcept = 0;
  /**
   * @brief Set the Execution State For App object
   *
   * @param app_id
   * @param state
   */
  virtual bool SetExecutionStateForApp(
      const uint16_t app_id,
      const ara::exec::ExecutionState state) noexcept = 0;
  /**
   * @brief Get the Execution State For App object
   *
   * @param app_id
   * @return std::optional<ara::exec::ExecutionState> last reported state
   */
  virtual std::optional<ara::exec::ExecutionState> GetExecutionStateForApp(
      const uint16_t app_id) noexcept = 0;
  /**
   * @brief Get the Actual Function Group ID object
   *
   * @return uint16_t currently active FG id
   */
  virtual uint16_t GetActualFunctionGroupID() noexcept = 0;
  /**
   * @brief Set the Actual Function Group ID object
   *
   * @param state_id new active FG id
   */
  virtual void SetActualFunctionGroupID(const uint16_t& state_id) noexcept = 0;
  virtual ~IAppDb() = default;
};

}  // namespace data
}  // namespace service
}  // namespace em
}  // namespace srp
#endif  // PLATFORM_COMMON_EM_CODE_SERVICES_EM_I_APP_DB_H_
