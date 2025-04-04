/**
 * @file diag_demon.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef APPS_COMMON_DIAG_DEMON_CODE_APPLICATION_DIAG_DEMON_H_
#define APPS_COMMON_DIAG_DEMON_CODE_APPLICATION_DIAG_DEMON_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/exec/adaptive_application.h"
#include "apps/common/diag_demon/code/services/uds_server.h"
// #include "apps/common/diag_demon/code/services/dtc/i_dtc_service.h"
namespace srp {
namespace platform {
namespace diag_demon {

class DiagDemon : public ::platform::exec::AdaptiveApplication {
 protected:
  uint32_t ecu_id{0};
  uint32_t eng_token{0};
  uint32_t plant_token{0};
  uint32_t mode{0};
  std::unique_ptr<uds::UdsServer> uds_server;
  // std::unique_ptr<dtc::IDtcService> dtc_service_;
  /**
   * @brief This function is called to launch the application
   *
   * @pplatformm token stop token
   */
  int Run(const std::stop_token& token) final;
  /**
   * @brief This function is called to initialiaze the application
   *
   * @pplatformm parms map with parms
   */
  int Initialize(
      const std::map<::platform::core::StringView, ::platform::core::StringView> parms) final;

 public:
  ~DiagDemon() = default;
};

}  // namespace diag_demon
}  // namespace platform
}  // namespace srp

#endif  // APPS_COMMON_DIAG_DEMON_CODE_APPLICATION_DIAG_DEMON_H_
