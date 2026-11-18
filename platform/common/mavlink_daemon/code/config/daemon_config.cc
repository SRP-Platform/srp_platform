/**
 * Copyright (c) 2024
 *
 * @file daemon_config.cc
 */
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "ara/com/com_error_domain.h"

namespace srp {
namespace mavlink_daemon {
namespace {
using nlohmann::json;

ara::core::ErrorCode Invalid(const std::string& what) {
  return ara::com::MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, what);
}

void Join(std::string* all, const std::string& problem) {
  *all += (all->empty() ? "" : "; ") + problem;
}

/// Reads an optional field; a wrong type is a problem.
template <typename T>
bool Get(const json& obj, const char* key, T* out, const std::string& where, std::string* problems) {
  if (!obj.contains(key)) {
    return false;
  }
  try {
    *out = obj.at(key).get<T>();
    return true;
  } catch (const json::exception&) {
    Join(problems, where + ": field '" + key + "' has the wrong type");
    return false;
  }
}

bool InRange(std::int64_t v, std::int64_t lo, std::int64_t hi) { return v >= lo && v <= hi; }
}  // namespace

ara::core::Result<DaemonConfig> ParseDaemonConfig(const json& root) {
  DaemonConfig cfg;
  std::string problems;
  if (!root.is_object()) {
    return Invalid("daemon config: the root must be a JSON object");
  }
  std::int64_t sys = 0;
  std::int64_t comp = 0;
  if (!Get(root, "system_id", &sys, "config", &problems) && !root.contains("system_id")) {
    Join(&problems, "missing field 'system_id'");
  }
  if (!Get(root, "component_id", &comp, "config", &problems) && !root.contains("component_id")) {
    Join(&problems, "missing field 'component_id'");
  }
  if (root.contains("system_id") && !InRange(sys, 1, 255)) {
    Join(&problems, "system_id out of range 1..255");
  }
  if (root.contains("component_id") && !InRange(comp, 1, 255)) {
    Join(&problems, "component_id out of range 1..255");
  }
  cfg.system_id = static_cast<std::uint8_t>(sys);
  cfg.component_id = static_cast<std::uint8_t>(comp);
  Get(root, "socket_path", &cfg.socket_path, "config", &problems);
  Get(root, "driver_dir", &cfg.driver_dir, "config", &problems);
  std::string mode;
  if (Get(root, "socket_mode", &mode, "config", &problems)) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(mode.c_str(), &end, 8);  // NOLINT(runtime/int)
    if (mode.empty() || *end != '\0' || v > 0777) {
      Join(&problems, "socket_mode '" + mode + "' is not an octal permission value (e.g. \"0660\")");
    } else {
      cfg.socket_mode = static_cast<unsigned>(v);
    }
  }
  std::int64_t delay = 0;
  if (Get(root, "restart_delay_ms", &delay, "config", &problems)) {
    if (delay < 0) {
      Join(&problems, "restart_delay_ms < 0");
    } else {
      cfg.restart_delay = std::chrono::milliseconds(delay);
    }
  }
  if (!root.contains("links") || !root.at("links").is_object()) {
    Join(&problems, "missing object 'links'");
  } else {
    std::map<std::uint8_t, std::string> link_ids;      // signing.link_id -> link
    std::map<std::string, std::string> state_files;    // non-empty state_file -> link
    for (const auto& [name, l] : root.at("links").items()) {
      const std::string where = "link '" + name + "'";
      if (name.empty()) {
        Join(&problems, "link with an empty name");
        continue;
      }
      if (!l.is_object()) {
        Join(&problems, where + ": must be an object");
        continue;
      }
      LinkConfigEntry e;
      e.name = name;
      if (!l.contains("driver")) {
        Join(&problems, where + ": missing field 'driver'");
      } else if (Get(l, "driver", &e.driver, where, &problems) && e.driver.empty()) {
        Join(&problems, where + ": 'driver' is empty");
      }
      Get(l, "driver_config", &e.driver_config, where, &problems);
      std::int64_t version = 2;
      if (Get(l, "version", &version, where, &problems) && version != 1 && version != 2) {
        Join(&problems, where + ": version must be 1 or 2");
      }
      e.version = static_cast<std::uint8_t>(version == 1 ? 1 : 2);
      if (Get(l, "heartbeat_hz", &e.heartbeat_hz, where, &problems) && e.heartbeat_hz < 0) {
        Join(&problems, where + ": heartbeat_hz < 0");
      }
      if (l.contains("signing")) {
        const auto& s = l.at("signing");
        if (!s.is_object()) {
          Join(&problems, where + ": 'signing' must be an object");
        } else {
          const std::string sw = where + " signing";
          Get(s, "enabled", &e.signing_enabled, sw, &problems);
          Get(s, "key_file", &e.key_file, sw, &problems);
          Get(s, "state_file", &e.state_file, sw, &problems);
          Get(s, "accept_unsigned", &e.accept_unsigned, sw, &problems);
          std::int64_t id = 0;
          if (Get(s, "link_id", &id, sw, &problems) && !InRange(id, 0, 255)) {
            Join(&problems, sw + ": link_id out of range 0..255");
          }
          e.signing_link_id = static_cast<std::uint8_t>(id);
          if (e.signing_enabled && e.key_file.empty()) {
            Join(&problems, sw + ": enabled=true requires 'key_file'");
          }
        }
      }
      if (e.signing_enabled && e.version == 1) {
        Join(&problems, where + ": signing requires MAVLink v2 (\"version\": 2)");
      }
      if (e.signing_enabled) {
        // A receiver merging redundant radios into one stream keeps one timestamp state per
        // (link_id, sysid, compid): the same link_id on two links = copies rejected as replay.
        const auto id = link_ids.emplace(e.signing_link_id, name);
        if (!id.second) {
          Join(&problems, where + " signing: link_id=" + std::to_string(e.signing_link_id) +
                              " repeated (link '" + id.first->second + "'); link_id must differ for every "
                              "signed link");
        }
        // Two signers sharing one state file would overwrite each other's timestamps.
        if (!e.state_file.empty()) {
          const auto sf = state_files.emplace(e.state_file, name);
          if (!sf.second) {
            Join(&problems, where + " signing: state_file '" + e.state_file + "' is the same as in link '" +
                                sf.first->second + "'");
          }
        }
      }
      cfg.links.push_back(std::move(e));
    }
  }
  if (!problems.empty()) {
    return Invalid("daemon config: " + problems);
  }
  return cfg;
}

ara::core::Result<DaemonConfig> LoadDaemonConfig(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    return Invalid("daemon config: cannot open '" + path + "'");
  }
  json root;
  try {
    root = json::parse(f);
  } catch (const json::exception& e) {
    return Invalid("daemon config '" + path + "': " + e.what());
  }
  auto r = ParseDaemonConfig(root);
  if (!r.HasValue()) {
    return Invalid(r.Error().SupportData() + " (file '" + path + "')");
  }
  return r;
}

}  // namespace mavlink_daemon
}  // namespace srp
