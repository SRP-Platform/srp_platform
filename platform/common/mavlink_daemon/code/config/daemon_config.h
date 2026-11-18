/**
 * Copyright (c) 2024
 *
 * @file daemon_config.h
 * @brief Konfiguracja MavlinkDaemona (plik JSON `mavlink_daemon_config.json`, w srp `deployment`).
 *
 * @code
 * {
 *   "system_id": 1,                       // system MAVLink CPU (1..255)
 *   "component_id": 1,                    // 1 na CPU (1..255)
 *   "socket_path": "/run/srp_mavlink_daemon.sock",   // opcjonalnie (to jest domyślna)
 *   "socket_mode": "0660",                // opcjonalnie: prawa gniazda (ósemkowo)
 *   "driver_dir": "/usr/lib/srp/mavlink_drivers",     // opcjonalnie: katalog z pluginami *.so
 *   "restart_delay_ms": 30000,            // opcjonalnie: przerwa przed ponownym Start() drivera
 *   "links": {
 *     "lora": {
 *       "driver": "serial_radio",         // id drivera (wbudowany albo z pluginu)
 *       "driver_config": "/etc/srp/mavlink/lora_serial.json",  // plik drivera; format zna tylko driver
 *       "version": 2,                     // 1 | 2 (domyślnie 2)
 *       "heartbeat_hz": 1.0,              // domyślnie 1, 0 = bez HEARTBEAT
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
  std::string driver_dir;  ///< pusty = bez pluginów
  std::chrono::milliseconds restart_delay{30000};
  std::vector<LinkConfigEntry> links;
};

/// Błąd: jedna wiadomość ze wszystkimi problemami (po średniku), z nazwami łączy.
ara::core::Result<DaemonConfig> ParseDaemonConfig(const nlohmann::json& json);
ara::core::Result<DaemonConfig> LoadDaemonConfig(const std::string& path);

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_CONFIG_DAEMON_CONFIG_H_
