/**
 * @file em_service.cc
 * @author Bartosz Snieg (snieg45@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-02-06
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "apps/common/em/code/services/em/em_service.h"

#include <bits/stdc++.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>  // NOLINT
#include <unordered_set>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>  // NOLINT

#include "platform/log/log.h"
#include "core/common/condition.h"
#include "apps/common/em/code/services/em/json_parser.h"

namespace srp {
namespace em {
namespace service {
namespace {
  constexpr auto kMax_wait_time = 3000;
  constexpr auto kSignal_check_interval = 100;
}

EmService::EmService(
    std::shared_ptr<data::IAppDb> db,
    const std::function<void(const uint16_t&)>&& update_callback)
    : db_{db}, update_callback_(std::move(update_callback)) {
  this->state_checker_thread = std::jthread([this](std::stop_token token){
    while (!token.stop_requested()) {
      core::condition::wait_for(std::chrono::milliseconds(kSignal_check_interval), token);
      for (const auto& app_id : current_fg_apps) {
        auto config = this->db_->GetAppConfig(app_id);
        if (!config.has_value()) {
          // TODO some DTC???
          continue;
        }
        if (kill(config.value().get().GetPid(), 0) == 0) {
          continue;
        }
        // TODO app dont work properly , add DTC
      }
    }
  })
}

EmService::~EmService() {}

bool EmService::IsSrpApp(const std::string& path) noexcept {
  std::ifstream file{path + "/etc/srp_app_config.json"};
  return file.is_open();
}

void EmService::LoadApps() noexcept {
  try {
    for (auto& p : std::filesystem::directory_iterator("/srp/opt")) {
      if (p.is_directory()) {
        if (this->IsSrpApp(p.path().c_str())) {
          std::string pp{p.path().string() + "/etc/srp_app_config.json"};
          auto res = json::JsonParser::GetAppConfig(pp);
          if (res.has_value()) {
            if (db_->InsertNewApp(res.value()) == 0) {
              platform::log::LogInfo()
                  << "App: " << res.value().GetAppName() << " added to db";
            }
          }
        }
      }
    }
  } catch (std::exception& e) {
    platform::log::LogError() << e.what();
  }
}

void EmService::SetActiveState(const uint16_t& state_id_) noexcept {
  std::vector<uint16_t> terminate_list{};
  const auto currect_list_opt = db_->GetFgAppList(active_state);
  const auto next_list_opt = db_->GetFgAppList(state_id_);
  if (!next_list_opt.has_value()) {
    platform::log::LogError() << "State: " << state_id_ << " not supported!";
    return;
  }
  const auto& next_list = next_list_opt.value().get();
  if (currect_list_opt.has_value()) {
    const auto& currect_list = currect_list_opt.value().get();
    for (const auto& app_id : currect_list) {
      if (!next_list.contains(app_id)) {
        terminate_list.push_back(app_id);
      }
    }
  }
  this->KillApps(terminate_list);
  for (const auto& app_id_ : next_list) {
    auto app_config_opt = db_->GetAppConfig(app_id_);
    if (app_config_opt.has_value()) {
      const auto& app_config = app_config_opt.value().get();
      if (app_config.GetPid() == 0) {
        const auto new_pid = this->StartApp(app_config);
        db_->SetPidForApp(app_id_, new_pid);
        if (!WaitForAppStatus(app_id_, ara::exec::ExecutionState::kRunning)) {
            //TODO(matik) report DTC app not start properly
          KillApp(app_id_, true);
        }
      }
    }
  }
  this->current_fg_apps = next_list;
  if (update_callback_) {
    update_callback_(state_id_);
  }
  this->active_state = state_id_;
}

bool EmService::WaitForAppStatus(const uint16_t& app_id_, const ara::exec::ExecutionState state) {
  auto app_config = db_->GetAppConfig(app_id_);
  if (!app_config.has_value()) {
    // TODO(matik) CALL DTC ERROR
    return false;
  }
  ara::exec::ExecutionState state_;
  auto start_time = std::chrono::high_resolution_clock::now();
  int64_t duration;
  do {
    app_config = db_->GetAppConfig(app_id_);
    state_ = app_config.value().get().GetExecutionState();
    auto now = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
  } while ((state != state_) && (duration <= kMax_wait_time));
  if (state == state_) {
    return true;
  }
  // TODO report DTC error
  return false;
}
// TODO bindings/common/socket/processocket
void EmService::KillApps(const std::vector<uint16_t>& terminate_list) {
for (const auto& app_id_ : terminate_list) {
  auto app_config = db_->GetAppConfig(app_id_);
  if (!app_config.has_value()) {
    // TODO(matik) Report DTC error
    break;
  }
  KillApp(app_config.value().get().GetPid());
  if (!this->WaitForAppStatus(app_id_, ara::exec::ExecutionState::kTerminated)) {
    //TODO report DTC (cant stop app, need to be killed)
    KillApp(app_config.value().get().GetPid(), true);
  }
}
}

void EmService::KillApp(const pid_t pid, bool force) {
  if (pid <= 0) {
    ara::log::LogWarn() << "Invalid pid value: " << std::to_string(static_cast<int>(pid));
    return;
  }
  if (force) {
    kill(pid, SIGKILL);
  } else {
    kill(pid, SIGTERM);
  }
}

std::optional<pid_t> EmService::RestartApp(const uint16_t appID) {
  return std::nullopt;
}

pid_t EmService::StartApp(const srp::em::service::data::AppConfig& app) {
  pid_t pid{0};
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
  auto path = app.GetBinPath();
  auto parms = app.GetParms();
  char* argv[] = {path.data(), parms.data(), NULL};
  posix_spawnp(&pid, app.GetBinPath().c_str(), NULL, &attr, argv, NULL);
  platform::log::LogInfo() << "Spawning app: " << app.GetAppName()
                      << " pid: " << std::to_string(pid);
  return pid;
}

}  // namespace service
}  // namespace em
}  // namespace srp
