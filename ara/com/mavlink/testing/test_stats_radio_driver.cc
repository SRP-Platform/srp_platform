/**
 * Copyright (c) 2024
 *
 * @file test_stats_radio_driver.cc
 * @brief TESTONLY: registers 2 drivers on a MemoryRadio for the end-to-end test of MavlinkLinkService
 * on the real ipc: `test_stats_a` reports no radio measurements (`valid` = 0 in the service),
 * `test_stats_b` pushes OnLinkStats{rssi = -71 dBm, noise floor = -110 dBm} every 100 ms while it runs.
 * The driver config path is ignored.
 */
#include <chrono>  // NOLINT
#include <memory>
#include <stop_token>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <utility>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/com/mavlink/testing/memory_radio.h"

namespace {

class StatsRadio final : public ara::com::mavlink::IRadioDriver {
 public:
  StatsRadio(std::string name, bool report) : report_{report}, inner_{std::move(name)} {}
  ~StatsRadio() override { Stop(); }

  ara::core::Result<void> Start(ara::com::mavlink::IRadioListener& l, std::stop_token t) noexcept override {
    auto r = inner_.Start(l, t);
    if (r.HasValue() && report_) {
      ticker_ = std::jthread([this](std::stop_token st) {
        ara::com::mavlink::RadioLinkStats s;
        s.valid = ara::com::mavlink::RadioLinkStats::kRssi | ara::com::mavlink::RadioLinkStats::kNoiseFloor;
        s.rssi_dbm = -71;
        s.noise_floor_dbm = -110;
        while (!st.stop_requested()) {
          inner_.LinkStats(s);  // posted to the driver thread of the MemoryRadio
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      });
    }
    return r;
  }
  void Stop() noexcept override {
    if (ticker_.joinable()) {
      ticker_.request_stop();
      ticker_.join();
    }
    inner_.Stop();
  }
  ara::core::Result<void> Send(std::span<const std::uint8_t> f) noexcept override { return inner_.Send(f); }
  std::string Name() const noexcept override { return inner_.Name(); }

 private:
  bool report_;
  ara::com::mavlink::testing::MemoryRadio inner_;
  std::jthread ticker_;
};

ara::com::mavlink::RadioDriverFactory Make(const std::string& name, bool report) {
  return [name, report](const std::string&) {
    return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<StatsRadio>(name, report)};
  };
}

}  // namespace

SRP_MAVLINK_REGISTER_DRIVER("test_stats_a", Make("test_stats_a", false));
SRP_MAVLINK_REGISTER_DRIVER("test_stats_b", Make("test_stats_b", true));
