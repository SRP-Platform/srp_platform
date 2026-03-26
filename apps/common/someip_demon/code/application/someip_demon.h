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

#ifndef APPS_COMMON_SOMEIP_DEMON_CODE_APPLICATION_SOMEIP_DEMON_H_
#define APPS_COMMON_SOMEIP_DEMON_CODE_APPLICATION_SOMEIP_DEMON_H_

#include <condition_variable>  // NOLINT
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>

#include "platform/exec/adaptive_application.h"
#include "platform/log/log.h"

namespace srp {
namespace someip_demon {
class SomeIpApplication final : public platform::exec::AdaptiveApplication {
 private:
  /**
   * @brief This function is called to initialiaze the application
   *
   * @pplatformm parms map with parms
   */
  int Initialize(
      const std::map<::platform::core::StringView, ::platform::core::StringView>
          parms) override;
  /**
   * @brief This function is called to launch the application
   *
   * @pplatformm token stop token
   */
  int Run(const std::stop_token& token) override;

 public:
  SomeIpApplication(/* args */);
  ~SomeIpApplication();
};

}  // namespace someip_demon
}  // namespace srp

#endif  // APPS_COMMON_SOMEIP_DEMON_CODE_APPLICATION_SOMEIP_DEMON_H_
