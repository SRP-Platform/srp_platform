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
#include "ara/exec/adaptive_lifecycle_menager.h"

#include <pthread.h>

#include <memory>
#include <string>
#include <utility>

#include "ara/exec/em/execution_client.h"

namespace ara {
namespace exec {

std::shared_ptr<AdaptiveLifecycleMenager> AdaptiveLifecycleMenager::instance_{
nullptr};

int AdaptiveLifecycleMenager::StartAdaptiveLifecycleMenager() {
  if (instance_ == nullptr) {
    return -1;
  }
  auto exec_client = ExecutionClient::GetInstance();
  exec_client->ReportExecutionState(ExecutionState::kIdle);
  if (instance_->app_thread_ == nullptr) {
    exec_client->ReportExecutionState(ExecutionState::kStarting);
    const int init_res = instance_->InitApp();
    if (init_res != 0) {
      exec_client->ReportExecutionState(ExecutionState::kErrorShutdown);
      instance_->exec_logger.LogError()
          << "Application Initialize failed: " << std::to_string(init_res);
      return init_res;
    }

    instance_->app_thread_ = std::make_unique<std::jthread>(
        [&](std::stop_token token) { instance_->Run(token); });
    pthread_setname_np(instance_->app_thread_->native_handle(), "APP_THREAD");
    exec_client->ReportExecutionState(ExecutionState::kRunning);
  }

  instance_->app_thread_->join();
  exec_client->ReportExecutionState(ExecutionState::kTerminating);
  instance_->exec_logger.LogInfo() << "Application Stoped";
  return 0;
}
void AdaptiveLifecycleMenager::StopAdaptiveLifecycleMenager(int status_) {
  if (instance_ == nullptr) {
    return;
  }
  instance_->exec_logger.LogInfo() << "Application Stoped requested";
  if (instance_->app_thread_ != nullptr) {
    instance_->app_thread_->request_stop();
  }
}

void AdaptiveLifecycleMenager::Run(const std::stop_token &token) {
  exec_logger.LogInfo() << "Starting application ";
  this->app_->Run(token);
}
int AdaptiveLifecycleMenager::InitApp() {
  return this->app_->Initialize(this->parms_);
}
std::pair<ara::core::StringView, ara::core::StringView>
AdaptiveLifecycleMenager::ParseParm(const ara::core::StringView &raw) {
  ara::core::StringView name{};
  ara::core::StringView value{};
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
      exec_logger{ara::log::LoggingMenager::GetInstance()->CreateLogger(
          "exec", "", ara::log::LogLevel::kWarn)} {
  const auto help_path = ara::core::StringView{argv[0]};
  const auto app_path = help_path.substr(0, help_path.find_last_of("/") - 3);

  exec_logger.LogDebug() << "App path: " << app_path;

  this->parms_.insert(std::make_pair("app_path", app_path));

  for (int i = 1; i < argc; i++) {
    this->parms_.insert(this->ParseParm(ara::core::StringView{argv[i]}));
  }
}

AdaptiveLifecycleMenager::~AdaptiveLifecycleMenager() {}

}  // namespace exec
}  // namespace ara
