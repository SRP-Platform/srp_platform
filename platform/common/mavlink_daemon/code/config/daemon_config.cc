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
#include <string_view>
#include <utility>

#include "ara/com/com_error_domain.h"

namespace srp {
namespace mavlink_daemon {
namespace {
// ordered_json: the order of the links in the file is the order of the config (link_index of the
// MavlinkLinkService); nlohmann::json would sort them by name.
using json = nlohmann::ordered_json;

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

/// Keys the daemon understands. Anything else is a startup error (a typo such as "heartbeat_Hz" must not be
/// silently ignored).
constexpr std::string_view kRootKeys[] = {"system_id", "component_id", "socket_path", "socket_mode",
                                          "restart_backoff", "stats_period_ms", "links"};
constexpr std::string_view kLinkKeys[] = {"driver", "driver_config", "air_rate_bps", "max_utilization", "mtu",
                                              "method_timeout_ms", "signing"};
/// Shortest slot (ms) of the LinkStats publication: stats_period_ms / number of links.
constexpr std::size_t kMinStatsSlotMs = 50;
constexpr std::string_view kSigningKeys[] = {"enabled", "key_file", "state_file", "accept_unsigned", "link_id"};

/// Keys that existed in earlier versions of the config: the error says what replaced them.
struct RemovedKey {
  std::string_view key;
  std::string_view hint;
};
constexpr RemovedKey kRemovedKeys[] = {
    {"version", "MAVLink v1 is not supported (the daemon speaks v2 only); remove the key"},
    {"heartbeat_hz", "the daemon sends no HEARTBEAT; a periodic heartbeat is an ordinary event of the application"},
    {"driver_dir", "radio drivers are linked statically; remove the key"},
    {"restart_delay_ms", "replaced by 'restart_backoff' {min_ms, max_ms}"},
    {"dedup_ttl_ms", "RX deduplication was removed; remove the key"},
};

template <std::size_t N>
bool Known(const std::string_view (&keys)[N], const std::string& key) {
  for (const auto k : keys) {
    if (k == key) {
      return true;
    }
  }
  return false;
}

/// Every key of `obj` must be in `keys`; the problem names the key and where it is.
template <std::size_t N>
void CheckKeys(const json& obj, const std::string_view (&keys)[N], const std::string& where,
               std::string* problems) {
  for (const auto& [key, value] : obj.items()) {
    if (Known(keys, key)) {
      continue;
    }
    std::string what = where + ": unknown key '" + key + "'";
    for (const auto& r : kRemovedKeys) {
      if (r.key == key) {
        what += " (" + std::string{r.hint} + ")";
        break;
      }
    }
    Join(problems, what);
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
  CheckKeys(root, kRootKeys, "config", &problems);
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
  if (root.contains("stats_period_ms")) {
    std::int64_t period = 0;
    if (Get(root, "stats_period_ms", &period, "config", &problems)) {
      if (!InRange(period, 10, 3600000)) {
        Join(&problems, "stats_period_ms out of range 10..3600000");
      } else {
        cfg.stats_period_ms = static_cast<std::uint32_t>(period);
      }
    }
  }
  if (root.contains("restart_delay_ms")) {
    Join(&problems, "restart_delay_ms was replaced by restart_backoff {min_ms, max_ms}");
  }
  if (root.contains("restart_backoff")) {
    const auto& rb = root.at("restart_backoff");
    if (!rb.is_object()) {
      Join(&problems, "restart_backoff must be an object {min_ms, max_ms}");
    } else {
      std::int64_t min_ms = cfg.restart_backoff.min.count();
      std::int64_t max_ms = cfg.restart_backoff.max.count();
      const bool min_ok = !rb.contains("min_ms") || Get(rb, "min_ms", &min_ms, "restart_backoff", &problems);
      const bool max_ok = !rb.contains("max_ms") || Get(rb, "max_ms", &max_ms, "restart_backoff", &problems);
      if (min_ok && max_ok) {
        if (min_ms < 1) {
          Join(&problems, "restart_backoff.min_ms < 1");
        } else if (max_ms < min_ms) {
          Join(&problems, "restart_backoff.max_ms < restart_backoff.min_ms");
        } else {
          cfg.restart_backoff.min = std::chrono::milliseconds(min_ms);
          cfg.restart_backoff.max = std::chrono::milliseconds(max_ms);
        }
      }
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
      CheckKeys(l, kLinkKeys, where, &problems);
      if (!l.contains("driver")) {
        Join(&problems, where + ": missing field 'driver'");
      } else if (Get(l, "driver", &e.driver, where, &problems) && e.driver.empty()) {
        Join(&problems, where + ": 'driver' is empty");
      }
      Get(l, "driver_config", &e.driver_config, where, &problems);
      std::int64_t air = 0;
      if (l.contains("air_rate_bps") && !l.at("air_rate_bps").is_number_integer()) {
        Join(&problems, where + ": field 'air_rate_bps' has the wrong type");
      } else if (Get(l, "air_rate_bps", &air, where, &problems)) {
        if (air < 1 || air > 0xFFFFFFFFLL) {
          Join(&problems, where + ": air_rate_bps must be in 1..4294967295");
        } else {
          e.air_rate_bps = static_cast<std::uint32_t>(air);
        }
      }
      if (Get(l, "max_utilization", &e.max_utilization, where, &problems) &&
          !(e.max_utilization > 0.0 && e.max_utilization <= 1.0)) {
        Join(&problems, where + ": max_utilization must be in (0, 1]");
        e.max_utilization = 0.8;
      }
      std::int64_t mtu = 0;
      if (l.contains("mtu") && !l.at("mtu").is_number_integer()) {
        Join(&problems, where + ": field 'mtu' has the wrong type");
      } else if (Get(l, "mtu", &mtu, where, &problems)) {
        if (mtu < 12 || mtu > 280) {
          Join(&problems, where + ": mtu must be in 12..280 B");
        } else {
          e.mtu = static_cast<std::uint32_t>(mtu);
        }
      }
      std::int64_t method_timeout = 0;
      if (l.contains("method_timeout_ms") && !l.at("method_timeout_ms").is_number_integer()) {
        Join(&problems, where + ": field 'method_timeout_ms' has the wrong type");
      } else if (Get(l, "method_timeout_ms", &method_timeout, where, &problems)) {
        if (method_timeout < 1 || method_timeout > 0xFFFFFFFFLL) {
          Join(&problems, where + ": method_timeout_ms must be in 1..4294967295");
        } else {
          e.method_timeout_ms = static_cast<std::uint32_t>(method_timeout);
        }
      }
      if (l.contains("signing")) {
        const auto& s = l.at("signing");
        if (!s.is_object()) {
          Join(&problems, where + ": 'signing' must be an object");
        } else {
          const std::string sw = where + " signing";
          CheckKeys(s, kSigningKeys, sw, &problems);
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
  // The links are published one per slot of stats_period_ms / links (the ipc event is a single slot); a slot
  // shorter than the wake-up latency of the receivers loses events.
  if (!cfg.links.empty() && cfg.stats_period_ms / cfg.links.size() < kMinStatsSlotMs) {
    Join(&problems, "stats_period_ms " + std::to_string(cfg.stats_period_ms) + " too short for " +
                        std::to_string(cfg.links.size()) + " links: stats_period_ms / links must be >= " +
                        std::to_string(kMinStatsSlotMs) + " ms");
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
