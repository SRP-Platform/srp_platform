/**
 * @file state_manager.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-12-30
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef APPS_COMMON_STATE_MANAGER_CODE_STATE_MANAGER_H_
#define APPS_COMMON_STATE_MANAGER_CODE_STATE_MANAGER_H_

#include <condition_variable>  // NOLINT
#include <map>
#include <memory>
#include <queue>
#include <string>

#include "platform/exec/adaptive_application.h"
#include "platform/exec/sm/i_state_controller.h"
#include "srp/platform/sm/StateManagerHandler.h"
// #include "platform/common/state_manager/code/did_impl.h"

namespace srp {
namespace sm {
class StateManager final : public ::platform::exec::AdaptiveApplication {
 private:
  /**
   * @brief This function is called to initialiaze the application
   *
   * @pplatformm parms map with parms
   */
  int Initialize(const std::map<::platform::core::StringView, ::platform::core::StringView>
                     parms) override;
  /**
   * @brief This function is called to launch the application
   *
   * @pplatformm token stop token
   */
  int Run(const std::stop_token& token) override;
  std::shared_ptr<::platform::exec::sm::IStateController> state_con_;
  srp::platform::sm::StateManagerProxy::Handler handler_;
  // std::unique_ptr<DiDImpl> did_;
 public:
  StateManager(/* args */);
  ~StateManager();
};
}  // namespace sm
}  // namespace srp
#endif  // APPS_COMMON_STATE_MANAGER_CODE_STATE_MANAGER_H_
