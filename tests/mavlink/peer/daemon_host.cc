/**
 * @file daemon_host.cc
 * @copyright Copyright (c) 2024
 * @brief Host MavlinkDaemona do testow E2E na PC: ten sam rdzen co `mavlink_daemon`
 * (`MavlinkDaemonCore`), ale bez Execution Management (nie ma go na hoscie) i ze sterowaniem
 * przez stdin/stdout (JSON lines), jak peer.
 *
 * Uzycie: daemon_host <mavlink_daemon_config.json>
 * stdout: {"ev":"ready"} po starcie albo {"ev":"error","error":"..."} i kod 2.
 * stdin:  {"id":N,"cmd":"stats"} -> {"id":N,"ok":true,"result":{...liczniki laczy i daemona...}},
 *         {"cmd":"stop"} albo EOF -> zatrzymanie rdzenia (zapis stanu podpisu) i wyjscie.
 */
#include <chrono>  // NOLINT
#include <cstdint>
#include <iostream>
#include <mutex>  // NOLINT
#include <string>

#include "nlohmann/json.hpp"
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"
#include "platform/common/mavlink_daemon/code/core/mavlink_daemon_core.h"

namespace {
using json = nlohmann::json;

std::mutex g_out;
void Send(const json& j) {
  std::lock_guard<std::mutex> lock{g_out};
  std::cout << j.dump() << "\n";
  std::cout.flush();
}

json Stats(const srp::mavlink_daemon::MavlinkDaemonCore& core, const srp::mavlink_daemon::DaemonConfig& cfg) {
  json s = {{"rx_ok", 0}, {"rx_crc_err", 0}, {"rx_sig_err", 0}, {"rx_dropped", 0}, {"tx", 0},
            {"tx_dropped", 0}, {"tx_expired", 0}, {"heartbeats_tx", 0}, {"heartbeats_rx", 0}};
  json links = json::object();
  for (const auto& l : cfg.links) {
    const auto link = core.FindLink(l.name);
    if (!link) continue;
    const auto c = link->counters();
    json one = {{"rx_ok", c.rx_ok}, {"rx_crc_err", c.rx_crc_err}, {"rx_sig_err", c.rx_sig_err},
                {"rx_dropped", c.rx_dropped}, {"tx", c.tx}, {"tx_dropped", c.tx_dropped},
                {"tx_expired", c.tx_expired}, {"heartbeats_tx", c.heartbeats_tx},
                {"heartbeats_rx", c.heartbeats_rx}, {"ready", link->IsReady()}};
    for (auto it = one.begin(); it != one.end(); ++it) {
      if (it.value().is_number() && s.contains(it.key())) {
        s[it.key()] = s[it.key()].get<std::uint64_t>() + it.value().get<std::uint64_t>();
      }
    }
    links[l.name] = one;
  }
  const auto d = core.counters();
  s["rx_duplicate"] = d.duplicates;
  s["unknown_messages"] = d.unknown_messages;
  s["register_rejected"] = d.register_rejected;
  s["send_rejected"] = d.send_rejected;
  s["instances"] = core.instance_count();
  s["sessions"] = core.session_count();
  s["links"] = links;
  return s;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "uzycie: daemon_host <mavlink_daemon_config.json>\n";
    return 2;
  }
  auto cfg = srp::mavlink_daemon::LoadDaemonConfig(argv[1]);
  if (!cfg.HasValue()) {
    Send({{"ev", "error"}, {"error", std::string(cfg.Error().SupportData())}});
    return 2;
  }
  srp::mavlink_daemon::DaemonOptions opts;
  opts.log = [](ara::com::mavlink::LogLevel, const std::string& m) { std::cerr << m << std::endl; };
  srp::mavlink_daemon::MavlinkDaemonCore core{cfg.Value(), opts};
  const auto started = core.Start();
  if (!started.HasValue()) {
    Send({{"ev", "error"}, {"error", std::string(started.Error().SupportData())}});
    return 2;
  }
  Send({{"ev", "ready"}});
  std::string line;
  while (std::getline(std::cin, line)) {
    json req;
    try {
      req = json::parse(line);
    } catch (...) {
      continue;
    }
    const std::string cmd = req.value("cmd", "");
    if (cmd == "stop") {
      break;
    }
    if (cmd == "stats") {
      Send({{"id", req["id"]}, {"ok", true}, {"result", Stats(core, cfg.Value())}});
    }
  }
  core.Stop();
  return 0;
}
