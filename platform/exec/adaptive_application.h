/**
 * @file adaptive_application.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-08-19
 *
 * @copyright Copyright (c) 2024
 *
 */
#ifndef PLATFORM_EXEC_ADAPTIVE_APPLICATION_H_
#define PLATFORM_EXEC_ADAPTIVE_APPLICATION_H_

#include <condition_variable>  // NOLINT
#include <map>
#include <mutex>  // NOLINT
#include <stop_token>  // NOLINT

#include "platform/core/string_view.h"

namespace platform {
namespace exec {
class AdaptiveApplication {
 public:
  /**
   * @brief This function is called to initialiaze the application
   *
   * @pplatformm parms map with parms
   */
  virtual int Initialize(
      const std::map<platform::core::StringView, platform::core::StringView> parms) = 0;
  /**
   * @brief This function is called to launch the application
   *
   * @pplatformm token stop token
   */
  virtual int Run(const std::stop_token& token) = 0;

 public:
  virtual ~AdaptiveApplication() = default;
};
}  // namespace exec
}  // namespace platform
#endif  // PLATFORM_EXEC_ADAPTIVE_APPLICATION_H_
