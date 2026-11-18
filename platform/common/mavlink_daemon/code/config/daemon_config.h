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
 *   "restart_backoff": { "min_ms": 500, "max_ms": 30000 },  // optional: pause before restarting a driver
 *   "stats_period_ms": 1000,              // optional: period of the LinkStats event of MavlinkLinkService (10..3600000, and stats_period_ms / links >= 50)
 *   "links": {
 *     "lora": {
 *       "driver": "serial_radio",         // id of a driver linked into the daemon binary
 *       "driver_config": "/etc/srp/mavlink/lora_serial.json",  // driver file; only the driver knows the format
 *       "air_rate_bps": 2400,             // optional: radio air rate; without it no budget, no TX pacing
 *       "max_utilization": 0.8,           // optional (0..1, default 0.8): TX is paced to air_rate_bps x this
 *       "mtu": 58,                        // optional (B, 12..280): largest frame; the generator checks it
 *       "method_timeout_ms": 1000,        // optional (default 1000): REQ expiry on this link / caller wait
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
  std::uint32_t air_rate_bps{0};     ///< 0 = no budget and no TX pacing
  double max_utilization{0.8};       ///< share of air_rate_bps the TX pacing allows (0, 1]
  std::uint32_t mtu{0};              ///< B; 0 = unknown (the generator checks the frames against it)
  /// How long a REQ may wait on this link (queue and TX pacing) and how long the caller waits for
  /// the RESP of a method that uses this link; the effective timeout of a method is the maximum
  /// over its links.
  std::uint32_t method_timeout_ms{1000};
  bool signing_enabled{false};
  std::string key_file;
  std::uint8_t signing_link_id{0};
  std::string state_file;
  bool accept_unsigned{false};
};

/// Pause before restarting a driver after a failed Start() or a fatal error: `min` after the
/// first failure, doubled after every further failure up to `max`; back to `min` once a start
/// lasted long enough (RadioLinkConfig::stable_after).
struct RestartBackoff {
  std::chrono::milliseconds min{500};
  std::chrono::milliseconds max{30000};
};

struct DaemonConfig {
  std::uint8_t system_id{1};
  std::uint8_t component_id{1};
  std::string socket_path{"/run/srp_mavlink_daemon.sock"};
  unsigned socket_mode{0660};
  RestartBackoff restart_backoff;
  /// Period of the per-link `LinkStats` event (service MavlinkLinkService and the IPC statistics
  /// subscription).
  std::uint32_t stats_period_ms{1000};
  std::vector<LinkConfigEntry> links;
};

/// Error: one message with all problems (separated by semicolons), naming the links.
ara::core::Result<DaemonConfig> ParseDaemonConfig(const nlohmann::ordered_json& json);
ara::core::Result<DaemonConfig> LoadDaemonConfig(const std::string& path);

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CONFIG_DAEMON_CONFIG_H_
