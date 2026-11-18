/**
 * @file daemon_host.cc
 * @copyright Copyright (c) 2024
 * @brief MavlinkDaemon host for the end-to-end tests on a PC: the same core as `mavlink_daemon`
 * (`MavlinkDaemonCore`) without Execution Management (there is none on the host), controlled
 * through stdin/stdout as JSON lines. Built by `mavlink_daemon_binary(host = True)`; the radio
 * drivers are the ones linked into that binary.
 *
 * Usage: <binary> <mavlink_daemon_config.json>
 * stdout: {"ev":"ready"} once started, or {"ev":"error","error":"..."} and exit code 2.
 * stdin:  {"id":N,"cmd":"stats"} -> {"id":N,"ok":true,"result":{"links":{...},...}},
 *         {"cmd":"stop"} or EOF -> stops the core (saves the signing state) and exits.
 */
#include "platform/common/mavlink_daemon/code/application/daemon_host.h"

#include <chrono>  // NOLINT
#include <cstdint>
#include <iostream>
#include <mutex>  // NOLINT
#include <string>

#include "nlohmann/json.hpp"
#include "platform/common/mavlink_daemon/code/config/daemon_config.h"
#include "platform/common/mavlink_daemon/code/core/mavlink_daemon_core.h"

namespace srp {
namespace mavlink_daemon {
namespace {
using json = nlohmann::json;

std::mutex g_out;
void Send(const json& j) {
  std::lock_guard<std::mutex> lock{g_out};
  std::cout << j.dump() << "\n";
  std::cout.flush();
}

json Stats(const MavlinkDaemonCore& core, const DaemonConfig& cfg) {
  json links = json::object();
  for (const auto& l : cfg.links) {
    const auto link = core.FindLink(l.name);
    if (!link) continue;
    const auto c = link->counters();
    links[l.name] = {{"rx_ok", c.rx_ok}, {"rx_crc_err", c.rx_crc_err}, {"rx_sig_err", c.rx_sig_err},
                     {"rx_dropped", c.rx_dropped}, {"tx_dropped", c.tx_dropped}, {"tx_expired", c.tx_expired},
                     {"ready", link->IsReady()}};
  }
  const auto d = core.counters();
  return {{"register_rejected", d.register_rejected},
          {"send_rejected", d.send_rejected}, {"links", links}};
}
}  // namespace

int RunDaemonHost(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << (argc > 0 ? argv[0] : "daemon_host") << " <mavlink_daemon_config.json>\n";
    return 2;
  }
  auto cfg = LoadDaemonConfig(argv[1]);
  if (!cfg.HasValue()) {
    Send({{"ev", "error"}, {"error", std::string(cfg.Error().SupportData())}});
    return 2;
  }
  DaemonOptions opts;
  opts.log = [](ara::com::mavlink::LogLevel, const std::string& m) { std::cerr << m << std::endl; };
  MavlinkDaemonCore core{cfg.Value(), opts};
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

}  // namespace mavlink_daemon
}  // namespace srp
