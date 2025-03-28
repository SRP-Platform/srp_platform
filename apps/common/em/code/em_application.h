/**
 * @file em_application.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-06
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef APPS_COMMON_EM_CODE_EM_APPLICATION_H_
#define APPS_COMMON_EM_CODE_EM_APPLICATION_H_


#include <condition_variable>  // NOLINT
#include <map>
#include <memory>
#include <queue>
#include <string>

#include "platform/exec/adaptive_application.h"
#include "apps/common/em/code/services/em/em_service.h"
#include "srp/platform/sm/StateManager.h"
#include "core/common/wait_queue.h"

namespace srp {
namespace em {
class EmApplication final : public ::platform::exec::AdaptiveApplication {
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

  std::shared_ptr<service::EmService> em_service;
  std::shared_ptr<srp::platform::sm::StateManagerSkeleton> sm_service_;
  // service::SmService sm_service_;
  core::WaitQueue<uint16_t> cmd_list_{};
 public:
  EmApplication(/* args */);
  ~EmApplication();
};

}  // namespace em
}  // namespace srp

#endif  // APPS_COMMON_EM_CODE_EM_APPLICATION_H_
