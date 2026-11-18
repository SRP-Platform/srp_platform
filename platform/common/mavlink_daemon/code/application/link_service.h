/**
 * Copyright (c) 2024
 *
 * @file link_service.h
 * @brief The platform ara::com service MavlinkLinkService (platform_definition/mavlink_link_service)
 * offered by the MavlinkDaemon on `ipc`: the `LinkStats` event (one per link every
 * `stats_period_ms`) and the `GetLinkStats(link)` method (the link name packed as in link_name.h). The data come from the daemon
 * core (`MavlinkDaemonCore::GetLinkStats`); this class only maps them onto the service types.
 */
#ifndef PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_LINK_SERVICE_H_
#define PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_LINK_SERVICE_H_

#include <functional>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <utility>

#include "ara/com/mavlink/daemon_protocol.h"
#include "platform_definition/mavlink_link_service/link_name.h"
#include "srp/platform/mavlink/MavlinkLinkServiceSkeleton.h"

namespace srp {
namespace mavlink_daemon {

/// Daemon statistics -> the service data type.
inline srp::platform::mavlink::LinkStatsData ToServiceData(const ara::com::mavlink::daemon::LinkStats& s) {
  srp::platform::mavlink::LinkStatsData d{};
  d.link_index = s.link_index;
  srp::platform::mavlink::PackLinkName(s.link_name, &d.link_name_lo, &d.link_name_hi);
  d.ready = s.ready;
  d.age_ms = s.age_ms;
  d.valid = s.valid;
  d.rssi_dbm = s.rssi_dbm;
  d.noise_floor_dbm = s.noise_floor_dbm;
  d.snr_db = s.snr_db;
  d.rx_ok = s.rx_ok;
  d.rx_crc_err = s.rx_crc_err;
  d.rx_sig_err = s.rx_sig_err;
  d.tx_frames = s.tx_frames;
  d.tx_busy = s.tx_busy;
  d.tx_dropped = s.tx_dropped;
  d.tx_expired = s.tx_expired;
  d.budget_pct = s.budget_pct;
  return d;
}

class LinkService : public srp::platform::mavlink::MavlinkLinkServiceSkeleton {
 public:
  using Provider = std::function<std::optional<ara::com::mavlink::daemon::LinkStats>(const std::string&)>;

  LinkService(const ara::core::InstanceSpecifier& instance, Provider provider)
      : srp::platform::mavlink::MavlinkLinkServiceSkeleton{instance}, provider_{std::move(provider)} {}
  ~LinkService() override = default;

  /// The `LinkStats` event of one link.
  void Publish(const ara::com::mavlink::daemon::LinkStats& s) { this->LinkStats.Update(ToServiceData(s)); }

  /// The daemon core owns the links only between its Start() and Stop(); GetLinkStats is answered
  /// from them, so it fails ("unknown link") while the core is not running. The service is offered
  /// BEFORE the core starts (its statistics thread publishes through the skeleton, whose binding
  /// must be set up by then) and stopped AFTER the core stopped. `SetCoreRunning(false)` waits for
  /// a GetLinkStats that is just reading the core, so the core can be stopped right after it.
  void SetCoreRunning(bool running) {
    const std::lock_guard<std::mutex> lock{core_mutex_};
    core_running_ = running;
  }

 protected:
  ara::core::Result<srp::platform::mavlink::LinkStatsData> GetLinkStats(
      const srp::platform::mavlink::LinkNameData& link) override {
    const std::string name = srp::platform::mavlink::UnpackLinkName(link.link_name_lo, link.link_name_hi);
    std::optional<ara::com::mavlink::daemon::LinkStats> s;
    {
      const std::lock_guard<std::mutex> lock{core_mutex_};
      if (core_running_) {
        s = provider_(name);
      }
    }
    if (!s) {
      return ara::com::MakeErrorCode(ara::com::ComErrc::kFieldValueIsNotValid, "unknown link '" + name + "'");
    }
    return ToServiceData(*s);
  }

 private:
  Provider provider_;
  std::mutex core_mutex_;
  bool core_running_{false};  // guarded by core_mutex_
};

}  // namespace mavlink_daemon
}  // namespace srp

#endif  // PLATFORM_COMMON_MAVLINK_DAEMON_CODE_APPLICATION_LINK_SERVICE_H_
