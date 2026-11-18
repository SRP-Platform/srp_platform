/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon.cc
 */
#include "platform/common/mavlink_daemon/code/application/mavlink_daemon.h"

#include <cstdlib>
#include <string>

#include "ara/log/log.h"
#include "core/common/condition.h"
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"

namespace srp {
namespace mavlink_daemon {
namespace {
void LogToAra(ara::com::mavlink::LogLevel level, const std::string& msg) {
  switch (level) {
    case ara::com::mavlink::LogLevel::kDebug:
      ara::log::LogDebug() << msg;
      break;
    case ara::com::mavlink::LogLevel::kInfo:
      ara::log::LogInfo() << msg;
      break;
    case ara::com::mavlink::LogLevel::kWarn:
      ara::log::LogWarn() << msg;
      break;
    case ara::com::mavlink::LogLevel::kError:
      ara::log::LogError() << msg;
      break;
  }
}
}  // namespace

int MavlinkDaemonApplication::Initialize(const std::map<ara::core::StringView, ara::core::StringView> parms) {
  const auto it = parms.find("app_path");
  if (it == parms.end()) {
    ara::log::LogError() << "mavlink_daemon: brak parametru app_path";
    return EXIT_FAILURE;
  }
  const std::string path = it->second + "mavlink_daemon_config.json";
  auto cfg = LoadDaemonConfig(path);
  if (!cfg.HasValue()) {
    ara::log::LogError() << "mavlink_daemon: " << cfg.Error().SupportData();
    return EXIT_FAILURE;
  }
  DaemonOptions opts;
  opts.log = LogToAra;
  core_ = std::make_unique<MavlinkDaemonCore>(cfg.Value(), opts);
  return EXIT_SUCCESS;
}

int MavlinkDaemonApplication::Run(const std::stop_token& token) {
  const auto started = core_->Start();
  if (!started.HasValue()) {
    ara::log::LogError() << "mavlink_daemon: błąd startu: " << started.Error().SupportData();
    return EXIT_FAILURE;
  }
  core::condition::wait(token);
  core_->Stop();
  return EXIT_SUCCESS;
}

}  // namespace mavlink_daemon
}  // namespace srp
