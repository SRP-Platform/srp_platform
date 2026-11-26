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
    ara::log::LogError() << "mavlink_daemon: missing parameter app_path";
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
  // Statistics of the links: the LinkStats event of the platform service (one per link per period) and
  // GetLinkStats(link_name). The service is offered on ipc only (platform/common/mavlink_daemon/model).
  link_service_ = std::make_unique<LinkService>(
      ara::core::InstanceSpecifier{"srp/platform/com/mavlink_daemon/LinkServicePPort"},
      [this](const std::string& link) { return core_->GetLinkStats(link); });
  opts.on_link_stats = [this](const ara::com::mavlink::daemon::LinkStats& s) { link_service_->Publish(s); };
  core_ = std::make_unique<MavlinkDaemonCore>(cfg.Value(), opts);
  return EXIT_SUCCESS;
}

int MavlinkDaemonApplication::Run(const std::stop_token& token) {
  // Offer first: the core's statistics thread publishes through the skeleton from its Start() on, and
  // the binding (send callback) is set up by StartOffer(). Stop in reverse order: the core first (its
  // threads stop publishing), then the offer.
  link_service_->StartOffer();
  const auto started = core_->Start();
  if (!started.HasValue()) {
    ara::log::LogError() << "mavlink_daemon: start error: " << started.Error().SupportData();
    link_service_->StopOffer();
    return EXIT_FAILURE;
  }
  link_service_->SetCoreRunning(true);
  core::condition::wait(token);
  link_service_->SetCoreRunning(false);
  core_->Stop();
  link_service_->StopOffer();
  return EXIT_SUCCESS;
}

}  // namespace mavlink_daemon
}  // namespace srp
