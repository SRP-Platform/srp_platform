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
#include <sstream>
#include <string>
#include <utility>
#include <thread>  // NOLINT
#include <vector>
#include <memory>

#include "ara/com/i_com_client.h"
#include "ara/exec/em/i_execution_client.h"
#include "ara/log/log.h"
#include "platform/common/em/code/services/em/json_parser.h"
#include "srp/platform/em/ExecutionHeader.h"
#include "core/common/condition.h"

namespace srp {
namespace em {
namespace service {
namespace {
constexpr auto kSignal_check_interval = 100;
constexpr auto kWatchdog_interval_ms = 500;
constexpr const char* kExec_path = "ARA.EXEC";
}  // namespace

void EmService::ProcessSockCallback(const uint32_t /*pid*/,
                                    const std::vector<uint8_t>& buf) noexcept {
  HandleExecFrame(buf);
}

void EmService::HandleExecFrame(const std::vector<uint8_t>& buf) noexcept {
  if (buf.empty() || buf[0] != ara::com::IComClient::MsgType::kExec) {
    return;
  }
  const std::vector<uint8_t> payload(buf.begin() + 1, buf.end());
  auto hdr_ =
      srp::data::Convert<srp::platform::em::ExecutionHeader>::Conv(payload);
  if (!hdr_.has_value()) {
    ara::log::LogError() << "Invalid ExecutionHeader payload";
    return;
  }
  if (hdr_.value().app_id > 0xFFFFU) {
    ara::log::LogError() << "app_id out of range: " << hdr_.value().app_id;
    return;
  }
  const uint16_t app_id = static_cast<uint16_t>(hdr_.value().app_id);
  ara::exec::ExecutionState state =
      static_cast<ara::exec::ExecutionState>(hdr_.value().execution_state);
  ara::log::LogDebug() << std::to_string(app_id)
                       << ", reported state: " << ara::exec::get_string(state);
  if (!this->db_->SetExecutionStateForApp(app_id, state)) {
    ara::log::LogError() << "Unknown app reported execution state: " << app_id;
  }
}

EmService::EmService(
    std::shared_ptr<data::IAppDb> db,
    const std::function<void(const uint16_t&)>&& update_callback,
    int start_timeout_ms, int stop_timeout_ms)
    : db_{db},
      update_callback_(std::move(update_callback)),
      proc_sock_(kExec_path),
      start_timeout_ms_{start_timeout_ms},
      stop_timeout_ms_{stop_timeout_ms} {
  proc_sock_.SetCallback(std::bind(&EmService::ProcessSockCallback, this,
                                   std::placeholders::_1,
                                   std::placeholders::_2));
  const auto err = proc_sock_.Offer();
  if (!err.HasValue()) {
    ara::log::LogError() << "Failed to offer ARA.EXEC socket";
    ipc_offered_ = false;
  } else {
    ipc_offered_ = true;
  }
  watchdog_ = std::make_unique<std::jthread>(
      [this](std::stop_token token) { this->WatchdogLoop(token); });
}

EmService::~EmService() {
  running_ = false;
  if (watchdog_ != nullptr) {
    watchdog_->request_stop();
    watchdog_.reset();
  }
  if (ipc_offered_) {
    proc_sock_.StopOffer();
  }
}

void EmService::WatchdogLoop(std::stop_token token) {
  while (!token.stop_requested()) {
    CheckAliveApps();
    core::condition::wait_for(std::chrono::milliseconds(kWatchdog_interval_ms),
                              token);
  }
}

void EmService::CheckAliveApps() {
  std::lock_guard<std::mutex> lock(service_mtx_);
  const auto fg_id = db_->GetActualFunctionGroupID();
  const auto list_opt = db_->GetFgAppList(fg_id);
  if (!list_opt.has_value()) {
    return;
  }
  for (const auto app_id : list_opt.value()) {
    const pid_t pid = db_->GetPidForApp(app_id);
    if (pid <= 0) {
      continue;
    }
    if (kill(pid, 0) == 0) {
      continue;
    }
    if (errno != ESRCH) {
      continue;
    }
    ara::log::LogError() << "App " << app_id << " died (pid "
                         << std::to_string(static_cast<int>(pid)) << ")";
    db_->SetPidForApp(app_id, 0);
    db_->SetExecutionStateForApp(app_id,
                                 ara::exec::ExecutionState::kErrorShutdown);
    const auto cfg = db_->GetAppConfig(app_id);
    if (!cfg.has_value()) {
      continue;
    }
    const pid_t new_pid = StartApp(cfg.value());
    if (new_pid <= 0) {
      continue;
    }
    db_->SetPidForApp(app_id, static_cast<uint32_t>(new_pid));
    if (!WaitForAppStatus(app_id, ara::exec::ExecutionState::kRunning,
                          start_timeout_ms_)) {
      KillApp(new_pid, true);
      ReapPid(new_pid);
      db_->SetPidForApp(app_id, 0);
      db_->SetExecutionStateForApp(app_id,
                                   ara::exec::ExecutionState::kErrorShutdown);
    }
  }
}

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
  std::lock_guard<std::mutex> lock(service_mtx_);
  std::vector<uint16_t> terminate_list{};
  const auto currect_list_opt = db_->GetFgAppList(active_state);
  const auto next_list_opt = db_->GetFgAppList(state_id_);
  if (!next_list_opt.has_value()) {
    ara::log::LogError() << "State: " << state_id_ << " not supported!";
    return;
  }
  const auto next_list = next_list_opt.value();
  if (currect_list_opt.has_value()) {
    const auto currect_list = currect_list_opt.value();
    for (const auto& app_id : currect_list) {
      if (!next_list.contains(app_id)) {
        terminate_list.push_back(app_id);
      }
    }
  }
  KillApps(terminate_list);
  this->db_->SetActualFunctionGroupID(state_id_);

  std::vector<std::pair<uint16_t, pid_t>> started;
  bool failed = false;
  for (const auto& app_id_ : next_list) {
    auto app_config_opt = db_->GetAppConfig(app_id_);
    if (!app_config_opt.has_value()) {
      failed = true;
      continue;
    }
    const auto app_config = app_config_opt.value();
    if (app_config.GetPid() != 0) {
      continue;
    }
    const auto new_pid = this->StartApp(app_config);
    if (new_pid <= 0) {
      ara::log::LogError() << "Failed to spawn app: "
                           << app_config.GetAppName();
      db_->SetExecutionStateForApp(app_id_,
                                   ara::exec::ExecutionState::kErrorShutdown);
      failed = true;
      continue;
    }
    db_->SetPidForApp(app_id_, static_cast<uint32_t>(new_pid));
    started.push_back({app_id_, new_pid});
  }

  for (const auto& item : started) {
    if (!WaitForAppStatus(item.first, ara::exec::ExecutionState::kRunning,
                          start_timeout_ms_)) {
      KillApp(item.second, true);
      ReapPid(item.second);
      db_->SetPidForApp(item.first, 0);
      db_->SetExecutionStateForApp(item.first,
                                   ara::exec::ExecutionState::kErrorShutdown);
      failed = true;
    }
  }

  this->current_fg_apps = next_list;
  this->active_state = state_id_;
  if (!failed && update_callback_) {
    update_callback_(state_id_);
  } else if (failed) {
    ara::log::LogError()
        << "FG " << state_id_
        << " not confirmed: one or more apps failed to reach kRunning";
  }
}

bool EmService::WaitForAppStatus(const uint16_t& app_id_,
                                 const ara::exec::ExecutionState state,
                                 int timeout_ms) {
  const auto start_time = std::chrono::steady_clock::now();
  while (true) {
    const auto state_opt = db_->GetExecutionStateForApp(app_id_);
    if (!state_opt.has_value()) {
      return false;
    }
    if (state_opt.value() == state) {
      return true;
    }
    if (!running_) {
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
            .count();
    if (duration > timeout_ms) {
      ara::log::LogError() << "Timeout waiting for app " << app_id_
                           << " to reach " << ara::exec::get_string(state);
      return false;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kSignal_check_interval));
  }
}

void EmService::KillApps(const std::vector<uint16_t>& terminate_list) {
  for (const auto& app_id_ : terminate_list) {
    const pid_t pid = db_->GetPidForApp(app_id_);
    if (pid <= 0) {
      db_->SetPidForApp(app_id_, 0);
      continue;
    }
    KillApp(pid);
    if (!this->WaitForAppStatus(app_id_, ara::exec::ExecutionState::kTerminated,
                                stop_timeout_ms_)) {
      KillApp(pid, true);
      db_->SetExecutionStateForApp(app_id_,
                                   ara::exec::ExecutionState::kTerminated);
    }
    ReapPid(pid);
    db_->SetPidForApp(app_id_, 0);
  }
}

void EmService::ReapPid(const pid_t pid) {
  if (pid <= 0) {
    return;
  }
  int status = 0;
  for (int i = 0; i < 20; ++i) {
    const pid_t res = waitpid(pid, &status, WNOHANG);
    if (res == pid || (res == -1 && errno == ECHILD)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  waitpid(pid, &status, WNOHANG);
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
  std::lock_guard<std::mutex> lock(service_mtx_);
  const auto cfg = db_->GetAppConfig(appID);
  if (!cfg.has_value()) {
    return std::nullopt;
  }
  const pid_t old_pid = db_->GetPidForApp(appID);
  if (old_pid > 0) {
    KillApp(old_pid, true);
    ReapPid(old_pid);
    db_->SetPidForApp(appID, 0);
  }
  const pid_t new_pid = StartApp(cfg.value());
  if (new_pid <= 0) {
    return std::nullopt;
  }
  db_->SetPidForApp(appID, static_cast<uint32_t>(new_pid));
  if (!WaitForAppStatus(appID, ara::exec::ExecutionState::kRunning,
                        start_timeout_ms_)) {
    KillApp(new_pid, true);
    ReapPid(new_pid);
    db_->SetPidForApp(appID, 0);
    return std::nullopt;
  }
  return new_pid;
}

pid_t EmService::StartApp(const srp::em::service::data::AppConfig& app) {
  pid_t pid{0};
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
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

  const int spawn_res =
      posix_spawnp(&pid, app.GetBinPath().c_str(), NULL, &attr, argv.data(),
                   NULL);
  posix_spawnattr_destroy(&attr);
  if (spawn_res != 0) {
    ara::log::LogError() << "posix_spawnp failed for " << app.GetAppName()
                         << ": " << std::strerror(spawn_res);
    return 0;
  }
  if (pid <= 0) {
    ara::log::LogError() << "posix_spawnp returned invalid pid for "
                         << app.GetAppName();
    return 0;
  }

  ara::log::LogInfo() << "Spawning app: " << app.GetAppName()
                      << " pid: " << std::to_string(pid);
  return pid;
}

}  // namespace service
}  // namespace em
}  // namespace srp
