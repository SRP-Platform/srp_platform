/**
 * @file app_db.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-12-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_EM_CODE_SERVICES_EM_APP_DB_H_
#define APPS_COMMON_EM_CODE_SERVICES_EM_APP_DB_H_

#include <cstdint>
#include <shared_mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "apps/common/em/code/services/em/app_config.h"
#include "apps/common/em/code/services/em/i_app_db.h"
namespace srp {
namespace em {
namespace service {
namespace data {
class AppDb : public IAppDb {
 private:
  /**
   * @brief Map with app config
   *
   */
  std::unordered_map<uint16_t, AppConfig> app_list_{};
  mutable std::shared_mutex app_list_mtx_;
  /**
   * @brief Map with all FG, ID as key
   *
   */
  std::unordered_map<uint16_t, std::unordered_set<uint16_t>> fg_list_{};
  std::mutex fg_list_mtx_;
  /**
   * @brief Map with name and id
   *
   */
  std::unordered_map<std::string, uint16_t> fg_name_2_id{};
  std::mutex fg_n_2_id_mtx_;

 public:
  AppDb(/* args */);
  int8_t InsertNewApp(AppConfig app) noexcept override;
  int8_t InsertNewFG(uint16_t fg_id, const std::string& name) noexcept override;
  std::optional<std::reference_wrapper<const AppConfig>> GetAppConfig(
      const uint16_t& app_id_) noexcept override;
  void SetPidForApp(const uint16_t app_id,
                    const uint32_t pid) noexcept override;
  std::optional<std::reference_wrapper<const std::unordered_set<uint16_t>>>
  GetFgAppList(const uint16_t& fg_id) noexcept override;
  ~AppDb() = default;
  void SetExecutionStateForApp(const uint16_t app_id,
    const platform::exec::ExecutionState state) noexcept override;
};

}  // namespace data
}  // namespace service
}  // namespace em
}  // namespace srp
#endif  // APPS_COMMON_EM_CODE_SERVICES_EM_APP_DB_H_
