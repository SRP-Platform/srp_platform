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

/// Odczyt opcjonalnego pola; zły typ = problem.
template <typename T>
bool Get(const json& obj, const char* key, T* out, const std::string& where, std::string* problems) {
  if (!obj.contains(key)) {
    return false;
  }
  try {
    *out = obj.at(key).get<T>();
    return true;
  } catch (const json::exception&) {
    Join(problems, where + ": pole '" + key + "' ma zły typ");
    return false;
  }
}

bool InRange(std::int64_t v, std::int64_t lo, std::int64_t hi) { return v >= lo && v <= hi; }
}  // namespace

ara::core::Result<DaemonConfig> ParseDaemonConfig(const json& root) {
  DaemonConfig cfg;
  std::string problems;
  if (!root.is_object()) {
    return Invalid("konfiguracja daemona: korzeń musi być obiektem JSON");
  }
  std::int64_t sys = 0;
  std::int64_t comp = 0;
  if (!Get(root, "system_id", &sys, "konfiguracja", &problems) && !root.contains("system_id")) {
    Join(&problems, "brak pola 'system_id'");
  }
  if (!Get(root, "component_id", &comp, "konfiguracja", &problems) && !root.contains("component_id")) {
    Join(&problems, "brak pola 'component_id'");
  }
  if (root.contains("system_id") && !InRange(sys, 1, 255)) {
    Join(&problems, "system_id poza zakresem 1..255");
  }
  if (root.contains("component_id") && !InRange(comp, 1, 255)) {
    Join(&problems, "component_id poza zakresem 1..255");
  }
  cfg.system_id = static_cast<std::uint8_t>(sys);
  cfg.component_id = static_cast<std::uint8_t>(comp);
  Get(root, "socket_path", &cfg.socket_path, "konfiguracja", &problems);
  Get(root, "driver_dir", &cfg.driver_dir, "konfiguracja", &problems);
  std::string mode;
  if (Get(root, "socket_mode", &mode, "konfiguracja", &problems)) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(mode.c_str(), &end, 8);  // NOLINT(runtime/int)
    if (mode.empty() || *end != '\0' || v > 0777) {
      Join(&problems, "socket_mode '" + mode + "' nie jest prawami ósemkowymi (np. \"0660\")");
    } else {
      cfg.socket_mode = static_cast<unsigned>(v);
    }
  }
  std::int64_t delay = 0;
  if (Get(root, "restart_delay_ms", &delay, "konfiguracja", &problems)) {
    if (delay < 0) {
      Join(&problems, "restart_delay_ms < 0");
    } else {
      cfg.restart_delay = std::chrono::milliseconds(delay);
    }
  }
  if (!root.contains("links") || !root.at("links").is_object()) {
    Join(&problems, "brak obiektu 'links'");
  } else {
    std::map<std::uint8_t, std::string> link_ids;      // signing.link_id -> łącze
    std::map<std::string, std::string> state_files;    // niepusty state_file -> łącze
    for (const auto& [name, l] : root.at("links").items()) {
      const std::string where = "łącze '" + name + "'";
      if (name.empty()) {
        Join(&problems, "łącze o pustej nazwie");
        continue;
      }
      if (!l.is_object()) {
        Join(&problems, where + ": musi być obiektem");
        continue;
      }
      LinkConfigEntry e;
      e.name = name;
      if (!l.contains("driver")) {
        Join(&problems, where + ": brak pola 'driver'");
      } else if (Get(l, "driver", &e.driver, where, &problems) && e.driver.empty()) {
        Join(&problems, where + ": 'driver' pusty");
      }
      Get(l, "driver_config", &e.driver_config, where, &problems);
      std::int64_t version = 2;
      if (Get(l, "version", &version, where, &problems) && version != 1 && version != 2) {
        Join(&problems, where + ": version musi być 1 albo 2");
      }
      e.version = static_cast<std::uint8_t>(version == 1 ? 1 : 2);
      if (Get(l, "heartbeat_hz", &e.heartbeat_hz, where, &problems) && e.heartbeat_hz < 0) {
        Join(&problems, where + ": heartbeat_hz < 0");
      }
      if (l.contains("signing")) {
        const auto& s = l.at("signing");
        if (!s.is_object()) {
          Join(&problems, where + ": 'signing' musi być obiektem");
        } else {
          const std::string sw = where + " signing";
          Get(s, "enabled", &e.signing_enabled, sw, &problems);
          Get(s, "key_file", &e.key_file, sw, &problems);
          Get(s, "state_file", &e.state_file, sw, &problems);
          Get(s, "accept_unsigned", &e.accept_unsigned, sw, &problems);
          std::int64_t id = 0;
          if (Get(s, "link_id", &id, sw, &problems) && !InRange(id, 0, 255)) {
            Join(&problems, sw + ": link_id poza zakresem 0..255");
          }
          e.signing_link_id = static_cast<std::uint8_t>(id);
          if (e.signing_enabled && e.key_file.empty()) {
            Join(&problems, sw + ": enabled=true wymaga 'key_file'");
          }
        }
      }
      if (e.signing_enabled) {
        // Odbiorca łączący redundantne radia w jeden strumień ma jeden stan znaczników czasu na
        // (link_id, sysid, compid): ten sam link_id na dwóch łączach = kopie odrzucane jako replay.
        const auto id = link_ids.emplace(e.signing_link_id, name);
        if (!id.second) {
          Join(&problems, where + " signing: link_id=" + std::to_string(e.signing_link_id) +
                              " powtórzony (łącze '" + id.first->second + "'); link_id musi być różny dla każdego "
                              "podpisanego łącza");
        }
        // Dwa signery z jednym plikiem stanu nadpisywałyby sobie znaczniki czasu.
        if (!e.state_file.empty()) {
          const auto sf = state_files.emplace(e.state_file, name);
          if (!sf.second) {
            Join(&problems, where + " signing: state_file '" + e.state_file + "' ten sam co w łączu '" +
                                sf.first->second + "'");
          }
        }
      }
      cfg.links.push_back(std::move(e));
    }
  }
  if (!problems.empty()) {
    return Invalid("konfiguracja daemona: " + problems);
  }
  return cfg;
}

ara::core::Result<DaemonConfig> LoadDaemonConfig(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    return Invalid("konfiguracja daemona: nie można otworzyć '" + path + "'");
  }
  json root;
  try {
    root = json::parse(f);
  } catch (const json::exception& e) {
    return Invalid("konfiguracja daemona '" + path + "': " + e.what());
  }
  auto r = ParseDaemonConfig(root);
  if (!r.HasValue()) {
    return Invalid(r.Error().SupportData() + " (plik '" + path + "')");
  }
  return r;
}

}  // namespace mavlink_daemon
}  // namespace srp
