/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon.h
 * @brief MavlinkDaemon platform application (modelled on someip_demon): a thin wrapper of the
 * core (core/mavlink_daemon_core.h) in the ara::exec life cycle.
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_MAVLINK_DAEMON_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_MAVLINK_DAEMON_H_

#include <map>
#include <memory>
#include <stop_token>  // NOLINT

#include "ara/exec/adaptive_application.h"
#include "platform/common/mavlink_daemon/code/core/mavlink_daemon_core.h"

namespace srp {
namespace mavlink_daemon {

class MavlinkDaemonApplication final : public ara::exec::AdaptiveApplication {
 public:
  MavlinkDaemonApplication() = default;
  ~MavlinkDaemonApplication() override = default;

 private:
  /// Loads `mavlink_daemon_config.json` from the application directory (etc).
  int Initialize(const std::map<ara::core::StringView, ara::core::StringView> parms) override;
  /// Starts the daemon (plugins, drivers, links, IPC socket); a start error = EXIT_FAILURE.
  int Run(const std::stop_token& token) override;

  std::unique_ptr<MavlinkDaemonCore> core_;
};

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_MAVLINK_DAEMON_H_
