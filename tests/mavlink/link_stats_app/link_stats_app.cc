/**
 * @file link_stats_app.cc
 * @copyright Copyright (c) 2024
 */
#include "tests/mavlink/link_stats_app/link_stats_app.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "ara/core/instance_specifier.h"
#include "ara/log/log.h"
#include "core/common/condition.h"
#include "platform_definition/mavlink_link_service/link_name.h"
#include "srp/platform/mavlink/MavlinkLinkService/MavlinkLinkServiceHandler.h"

namespace srp {
namespace test {
namespace {
namespace svc = srp::platform::mavlink;

// One line per record, the same text on the console and in the log: the test greps for it.
void Print(const std::string& line) {
  ara::log::LogInfo() << line;
  std::fprintf(stderr, "%s\n", line.c_str());
}

std::string Describe(const svc::LinkStatsData& d) {
  return "link=" + svc::UnpackLinkName(d.link_name_lo, d.link_name_hi) + " idx=" + std::to_string(d.link_index) +
         " ready=" + std::to_string(d.ready) + " age_ms=" + std::to_string(d.age_ms) +
         " valid=" + std::to_string(d.valid) + " rssi=" + std::to_string(d.rssi_dbm) +
         " noise=" + std::to_string(d.noise_floor_dbm) + " snr=" + std::to_string(d.snr_db) +
         " rx_ok=" + std::to_string(d.rx_ok) + " rx_crc=" + std::to_string(d.rx_crc_err) +
         " rx_sig=" + std::to_string(d.rx_sig_err) + " tx=" + std::to_string(d.tx_frames) +
         " tx_busy=" + std::to_string(d.tx_busy) + " tx_drop=" + std::to_string(d.tx_dropped) +
         " tx_exp=" + std::to_string(d.tx_expired) + " budget=" + std::to_string(d.budget_pct);
}

void Query(const svc::MavlinkLinkServiceProxy::handler_t& handler, const std::string& name) {
  svc::LinkNameData in{};
  svc::PackLinkName(name, &in.link_name_lo, &in.link_name_hi);
  const auto r = handler->GetLinkStats(in);
  if (r.HasValue()) {
    Print("GETLINKSTATS ok " + Describe(r.Value()));
  } else {
    Print("GETLINKSTATS error link=" + name + " " + std::string(r.Error().SupportData()));
  }
}
}  // namespace

int LinkStatsApp::Initialize(const std::map<ara::core::StringView, ara::core::StringView> parms) {
  static_cast<void>(parms);
  return 0;
}

int LinkStatsApp::Run(const std::stop_token& token) {
  svc::MavlinkLinkServiceProxy proxy{ara::core::InstanceSpecifier{"srp/test/link_stats_app/LinkRPort"}};
  proxy.StartFindService([](svc::MavlinkLinkServiceProxy::handler_t handler) {
    Print("LINKSTATS service found");
    handler->LinkStats.Subscribe(8, [handler](const auto) {
      handler->LinkStats.SetReceiveHandler([handler]() {
        const auto s = handler->LinkStats.GetNewSamples();
        if (s.HasValue()) {
          Print("LINKSTATS " + Describe(s.Value()));
        }
      });
    });
    // Two calls: the link `radio_b` (it exists in link_stats_daemon_config.json) and `nie_ma` (no such link: an error).
    Query(handler, "radio_b");
    Query(handler, "nie_ma");
  });
  srp::core::condition::wait(token);
  proxy.StopFindService();
  return 0;
}

}  // namespace test
}  // namespace srp
