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
#include "platform/common/em/code/services/em/em_service.h"

#include <bits/stdc++.h>
#include <dirent.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/i_com_client.h"
#include "ara/exec/em/i_execution_client.h"
#include "ara/log/log.h"
#include "platform/common/em/code/services/em/json_parser.h"
#include "srp/platform/em/ExecutionHeader.h"

namespace srp {
namespace em {
namespace service {
namespace {
constexpr auto kMax_wait_time = 3000;
constexpr auto kSignal_check_interval = 100;
constexpr const char* kExec_path = "ARA.EXEC";
}  // namespace

void EmService::ProcessSockCallback(const uint32_t pid,
                                    const std::vector<uint8_t>& buf) noexcept {
  if (buf.empty() || buf[0] != ara::com::IComClient::MsgType::kExec) {
    return;
  }
  const std::vector<uint8_t> payload(buf.begin() + 1, buf.end());
  auto hdr_ = srp::data::Convert<srp::platform::em::ExecutionHeader>::Conv(
      payload);
  if (!hdr_.has_value()) {
    // TODO(matik) add dtc error
    return;
  }
  ara::exec::ExecutionState state =
      static_cast<ara::exec::ExecutionState>(hdr_.value().execution_state);
  ara::log::LogDebug() << std::to_string(hdr_.value().app_id)
                       << ", reported state: " << ara::exec::get_string(state);
  if (!this->db_->SetExecutionStateForApp(hdr_.value().app_id, state)) {
    // TODO(matik) add dtc error
    return;
  }
}

EmService::EmService(
    std::shared_ptr<data::IAppDb> db,
    const std::function<void(const uint16_t&)>&& update_callback)
    : db_{db},
      update_callback_(std::move(update_callback)),
      proc_sock_(kExec_path) {
  proc_sock_.SetCallback(std::bind(&EmService::ProcessSockCallback, this,
                                   std::placeholders::_1,
                                   std::placeholders::_2));
  const auto err = proc_sock_.Offer();
  if (!err.HasValue()) {
    ara::log::LogError() << "Failed to offer ARA.EXEC socket";
  }
}

EmService::~EmService() { proc_sock_.StopOffer(); }

bool EmService::IsSrpApp(const std::string& path) noexcept {
  std::ifstream file{path + "/etc/srp_app.json"};
  return file.is_open();
}

void EmService::LoadApps() noexcept {
  DIR* dir = opendir("/srp/opt");
  if (dir == nullptr) {
    return;
  }
  while (dirent* entry = readdir(dir)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    std::string path = std::string("/srp/opt/") + entry->d_name;
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      if (this->IsSrpApp(path.c_str())) {
        std::string pp{path + "/etc/srp_app.json"};
        auto res = json::JsonParser::GetAppConfig(pp);
        if (res.has_value()) {
          if (db_->InsertNewApp(res.value()) == 0) {
            ara::log::LogInfo()
                << "App: " << res.value().GetAppName() << " added to db";
          }
        }
      }
    }
  }
  closedir(dir);
}

void EmService::SetActiveState(const uint16_t& state_id_) noexcept {
  std::vector<uint16_t> terminate_list{};
  const auto currect_list_opt = db_->GetFgAppList(active_state);
  const auto next_list_opt = db_->GetFgAppList(state_id_);
  if (!next_list_opt.has_value()) {
    ara::log::LogError() << "State: " << state_id_ << " not supported!";
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
  KillApps(terminate_list);
  this->db_->SetActualFunctionGroupID(state_id_);
  for (const auto& app_id_ : next_list) {
    auto app_config_opt = db_->GetAppConfig(app_id_);
    if (app_config_opt.has_value()) {
      const auto& app_config = app_config_opt.value().get();
      if (app_config.GetPid() == 0) {
        const auto new_pid = this->StartApp(app_config);
        db_->SetPidForApp(app_id_, new_pid);
        if (!WaitForAppStatus(app_id_, ara::exec::ExecutionState::kRunning)) {
          KillApp(new_pid, true);
          db_->SetPidForApp(app_id_, 0);
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

bool EmService::WaitForAppStatus(const uint16_t& app_id_,
                                 const ara::exec::ExecutionState state) {
  auto app_config = db_->GetAppConfig(app_id_);
  if (!app_config.has_value()) {
    // TODO(matik) CALL DTC ERROR
    return false;
  }
  ara::exec::ExecutionState state_{ara::exec::ExecutionState::kIdle};
  const auto start_time = std::chrono::steady_clock::now();
  int64_t duration{0};
  do {
    app_config = db_->GetAppConfig(app_id_);
    if (!app_config.has_value()) {
      return false;
    }
    state_ = app_config.value().get().GetExecutionState();
    const auto now = std::chrono::steady_clock::now();
    duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
            .count();
    if (state != state_) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kSignal_check_interval));
    }
  } while ((state != state_) && (duration <= kMax_wait_time));
  if (state == state_) {
    return true;
  }
  // TODO(matik) report DTC error
  ara::log::LogError() << "Timeout waiting for app " << app_id_
                       << " to reach " << ara::exec::get_string(state);
  return false;
}

void EmService::KillApps(const std::vector<uint16_t>& terminate_list) {
  for (const auto& app_id_ : terminate_list) {
    auto app_config = db_->GetAppConfig(app_id_);
    if (!app_config.has_value()) {
      // TODO(matik) Report DTC error
      continue;
    }
    KillApp(app_config.value().get().GetPid());
    if (!this->WaitForAppStatus(app_id_,
                                ara::exec::ExecutionState::kTerminated)) {
      // TODO(matik) report DTC (cant stop app, need to be killed)
      KillApp(app_config.value().get().GetPid(), true);
      db_->SetExecutionStateForApp(app_config.value().get().GetAppId(),
                                   ara::exec::ExecutionState::kTerminated);
    }
    db_->SetPidForApp(app_id_, 0);
  }
}

void EmService::KillApp(const pid_t pid, bool force) {
  if (pid <= 0) {
    ara::log::LogWarn() << "Invalid pid value: "
                        << std::to_string(static_cast<int>(pid));
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

  std::vector<std::string> tokens;
  std::istringstream iss(parms);
  std::string token;
  while (iss >> token) {
    tokens.push_back(std::move(token));
  }

  std::vector<char*> argv;
  argv.reserve(tokens.size() + 2);
  argv.push_back(const_cast<char*>(path.c_str()));
  for (auto& t : tokens) {
    argv.push_back(t.data());
  }
  argv.push_back(NULL);

  posix_spawnp(&pid, app.GetBinPath().c_str(), NULL, &attr, argv.data(), NULL);

  posix_spawnattr_destroy(&attr);

  ara::log::LogInfo() << "Spawning app: " << app.GetAppName()
                      << " pid: " << std::to_string(pid);
  return pid;
}

}  // namespace service
}  // namespace em
}  // namespace srp
