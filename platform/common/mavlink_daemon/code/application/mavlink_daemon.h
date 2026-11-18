/**
 * Copyright (c) 2024
 *
 * @file mavlink_daemon.h
 * @brief Aplikacja platformy MavlinkDaemon (wzór: someip_demon): cienka obudowa rdzenia
 * (core/mavlink_daemon_core.h) w cyklu życia ara::exec.
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
  /// Wczytuje `mavlink_daemon_config.json` z katalogu aplikacji (etc).
  int Initialize(const std::map<ara::core::StringView, ara::core::StringView> parms) override;
  /// Startuje daemon (pluginy, drivery, łącza, gniazdo IPC); błąd startu = EXIT_FAILURE.
  int Run(const std::stop_token& token) override;

  std::unique_ptr<MavlinkDaemonCore> core_;
};

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_MAVLINK_DAEMON_H_
