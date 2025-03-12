/**
 * @file adaptive_lifecycle_menager.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-09-01
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "platform/exec/adaptive_lifecycle_menager.h"
#include "platform/exec/em/execution_client.h"

#include <pthread.h>
#include <unistd.h>

#include <string>
#include <utility>

namespace platform {
namespace exec {

std::shared_ptr<AdaptiveLifecycleMenager> AdaptiveLifecycleMenager::instance_{
    nullptr};

int AdaptiveLifecycleMenager::StartAdaptiveLifecycleMenager() {
  if (instance_ == nullptr) {
    return -1;
  }
  auto exec_client_ = ExecutionClient::GetInstance();
  exec_client_->ReportExecutionState(ExecutionState::kIdle);

  if (instance_->app_thread_ == nullptr) {
    exec_client_->ReportExecutionState(ExecutionState::kStarting);
    instance_->InitApp();
    exec_client_->ReportExecutionState(ExecutionState::kRunning);
    instance_->app_thread_ = std::make_unique<std::jthread>(
        [&](std::stop_token token) { instance_->Run(token); });
    pthread_setname_np(instance_->app_thread_->native_handle(), "APP_THREAD");
  }

  instance_->app_thread_->join();
  instance_->exec_logger.LogInfo() << "Application Stoped";
  // todo start app and wait
  exec_client_->ReportExecutionState(ExecutionState::kTerminated);
  return 0;
}
void AdaptiveLifecycleMenager::StopAdaptiveLifecycleMenager(int status_) {
  auto exec_client_ = ExecutionClient::GetInstance();
  instance_->exec_logger.LogInfo() << "Application Stoped requested";
  exec_client_->ReportExecutionState(ExecutionState::kTerminating);
  instance_->app_thread_->request_stop();
}

void AdaptiveLifecycleMenager::Run(const std::stop_token &token) {
  exec_logger.LogInfo() << "Starting application ";
  this->app_->Run(token);
}
void AdaptiveLifecycleMenager::InitApp() {
  this->app_->Initialize(this->parms_);
}
std::pair<platform::core::StringView, platform::core::StringView>
AdaptiveLifecycleMenager::ParseParm(const platform::core::StringView &raw) {
  platform::core::StringView name{};
  platform::core::StringView value{};
  if (raw.find("=") != std::string::npos) {
    name = raw.substr(0, raw.find("="));
    value = raw.substr(raw.find("=") + 1);
  } else {
    name = raw;
  }
  return std::make_pair(name, value);
}

AdaptiveLifecycleMenager::AdaptiveLifecycleMenager(
    int argc, char const *argv[], std::unique_ptr<AdaptiveApplication> app,
    bool controll)
    : app_{std::move(app)},
      exec_logger{platform::log::LoggingMenager::GetInstance()->CreateLogger(
          "exec", "", platform::log::LogLevel::kWarn)} {
  const auto help_path = platform::core::StringView{argv[0]};
  const auto app_path = help_path.substr(0, help_path.find_last_of("/") - 3);

  exec_logger.LogDebug() << "App path: " << app_path;
  chdir(app_path.c_str());

  for (int i = 1; i < argc; i++) {
    this->parms_.insert(this->ParseParm(platform::core::StringView{argv[i]}));
  }
}

AdaptiveLifecycleMenager::~AdaptiveLifecycleMenager() {}

}  // namespace exec
}  // namespace platform
