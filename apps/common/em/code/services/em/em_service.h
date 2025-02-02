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
#ifndef APPS_COMMON_EM_CODE_SERVICES_EM_EM_SERVICE_H_
#define APPS_COMMON_EM_CODE_SERVICES_EM_EM_SERVICE_H_
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app_config.h"
#include "apps/common/em/code/services/em/i_app_db.h"
#include "core/json/json_parser.h"

namespace srp {
namespace em {
namespace service {
class EmService {
 private:
  uint16_t active_state{0U};
  const std::shared_ptr<data::IAppDb> db_;
  const std::function<void(const uint16_t&)> update_callback_;
  bool IsSrpApp(const std::string& path) noexcept;

  pid_t StartApp(const srp::em::service::data::AppConfig& app);

 public:
  uint16_t GetActiveState() const { return active_state; }
  void LoadApps() noexcept;
  void SetActiveState(const uint16_t& state_id_) noexcept;
  std::optional<pid_t> RestartApp(const uint16_t appID);
  EmService(std::shared_ptr<data::IAppDb> db,
            const std::function<void(const uint16_t&)>&& update_callback);
  ~EmService();
};

}  // namespace service
}  // namespace em
}  // namespace srp
#endif  // APPS_COMMON_EM_CODE_SERVICES_EM_EM_SERVICE_H_
