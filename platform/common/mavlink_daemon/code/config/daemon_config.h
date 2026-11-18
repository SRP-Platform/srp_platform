/**
 * Copyright (c) 2024
 *
 * @file daemon_config.h
 * @brief MavlinkDaemon configuration (JSON file `mavlink_daemon_config.json`, in srp `deployment`).
 *
 * @code
 * {
 *   "system_id": 1,                       // MAVLink system of the CPU (1..255)
 *   "component_id": 1,                    // one per CPU (1..255)
 *   "socket_path": "/run/srp_mavlink_daemon.sock",   // optional (this is the default)
 *   "socket_mode": "0660",                // optional: socket permissions (octal)
 *   "driver_dir": "/usr/lib/srp/mavlink_drivers",     // optional: directory with *.so plugins
 *   "restart_delay_ms": 30000,            // optional: pause before restarting a driver
 *   "links": {
 *     "lora": {
 *       "driver": "serial_radio",         // driver id (built in or from a plugin)
 *       "driver_config": "/etc/srp/mavlink/lora_serial.json",  // driver file; only the driver knows the format
 *       "version": 2,                     // 1 | 2 (default 2); signing requires 2
 *       "heartbeat_hz": 1.0,              // default 1, 0 = no HEARTBEAT
 *       "signing": { "enabled": true, "key_file": "/etc/srp/mavlink/lora.key", "link_id": 1,
 *                    "state_file": "/var/lib/srp/mavlink/lora.state", "accept_unsigned": false }
 *     }
 *   }
 * }
 * @endcode
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CONFIG_DAEMON_CONFIG_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CONFIG_DAEMON_CONFIG_H_

#include <chrono>  // NOLINT
#include <cstdint>
#include <string>
#include <vector>

#include "ara/core/result.h"
#include "nlohmann/json.hpp"

namespace srp {
namespace mavlink_daemon {

struct LinkConfigEntry {
  std::string name;
  std::string driver;
  std::string driver_config;
  std::uint8_t version{2};
  double heartbeat_hz{1.0};
  bool signing_enabled{false};
  std::string key_file;
  std::uint8_t signing_link_id{0};
  std::string state_file;
  bool accept_unsigned{false};
};

struct DaemonConfig {
  std::uint8_t system_id{1};
  std::uint8_t component_id{1};
  std::string socket_path{"/run/srp_mavlink_daemon.sock"};
  unsigned socket_mode{0660};
  std::string driver_dir;  ///< empty = no plugins
  std::chrono::milliseconds restart_delay{30000};
  std::vector<LinkConfigEntry> links;
};

/// Error: one message with all problems (separated by semicolons), naming the links.
ara::core::Result<DaemonConfig> ParseDaemonConfig(const nlohmann::json& json);
ara::core::Result<DaemonConfig> LoadDaemonConfig(const std::string& path);

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CONFIG_DAEMON_CONFIG_H_
