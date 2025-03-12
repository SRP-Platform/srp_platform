/**
 * @file adaptive_lifecycle_menager.h
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2024-09-01
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#ifndef PLATFORM_EXEC_ADAPTIVE_LIFECYCLE_MENAGER_H_
#define PLATFORM_EXEC_ADAPTIVE_LIFECYCLE_MENAGER_H_

#include <map>
#include <memory>
#include <stop_token>  // NOLINT
#include <thread>  // NOLINT
#include <utility>

#include "platform/core/string_view.h"
#include "platform/exec/adaptive_application.h"
#include "platform/log/logging_menager.h"
namespace platform {
namespace exec {
class AdaptiveLifecycleMenager final {
 private:
  std::unique_ptr<AdaptiveApplication> app_;
  std::map<platform::core::StringView, platform::core::StringView> parms_{};
  static std::shared_ptr<AdaptiveLifecycleMenager> instance_;
  std::pair<platform::core::StringView, platform::core::StringView> ParseParm(
      const platform::core::StringView &raw);
  const platform::log::Logger &exec_logger;
  std::stop_source stop_source_;
  std::unique_ptr<std::jthread> app_thread_;

 public:
  void Run(const std::stop_token &token);
  void InitApp();
  AdaptiveLifecycleMenager(int argc, char const *argv[],
                           std::unique_ptr<AdaptiveApplication> app,
                           bool controll);

  template <typename APPTYPE, typename... Pplatformms>
  static void CreateAdaptiveLifecycleMenager(int argc, char const *argv[],
                                             bool controll, Pplatformms &&...args) {
    instance_ = std::make_shared<AdaptiveLifecycleMenager>(
        argc, argv, std::make_unique<APPTYPE>(std::forward(args)...), controll);
  }

  static int StartAdaptiveLifecycleMenager();
  static void StopAdaptiveLifecycleMenager(int status_);
  ~AdaptiveLifecycleMenager();
};

}  // namespace exec
}  // namespace platform

#endif  // PLATFORM_EXEC_ADAPTIVE_LIFECYCLE_MENAGER_H_
