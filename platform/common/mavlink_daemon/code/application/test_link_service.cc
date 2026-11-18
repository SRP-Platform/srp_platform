/**
 * @file test_link_service.cc
 * @copyright Copyright (c) 2024
 * @brief MavlinkLinkService: the mapping of the daemon statistics onto the service type, the wire
 * encoding of the generated type and GetLinkStats (known and unknown link).
 */
#include "platform/common/mavlink_daemon/code/application/link_service.h"

#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <thread>  // NOLINT
#include <tuple>

namespace srp {
namespace mavlink_daemon {
namespace {
namespace dp = ara::com::mavlink::daemon;
using srp::platform::mavlink::LinkNameData;
using srp::platform::mavlink::LinkStatsData;

dp::LinkStats Sample() {
  dp::LinkStats s;
  s.link_index = 1;
  s.link_name = "radio_b";
  s.ready = true;
  s.age_ms = 250;
  s.valid = 3;
  s.rssi_dbm = -71;
  s.noise_floor_dbm = -110;
  s.snr_db = 39;
  s.rx_ok = 11;
  s.rx_crc_err = 12;
  s.rx_sig_err = 13;
  s.tx_frames = 14;
  s.tx_busy = 15;
  s.tx_dropped = 16;
  s.tx_expired = 17;
  s.budget_pct = 18;
  return s;
}

/// Exposes the protected method handler of the skeleton.
class TestableService : public LinkService {
 public:
  using LinkService::LinkService;
  using LinkService::GetLinkStats;
};

TEST(LinkServiceMapping, AllFieldsAreCopiedAndTheNameIsPacked) {
  const auto d = ToServiceData(Sample());
  EXPECT_EQ(d.link_index, 1);
  EXPECT_EQ(srp::platform::mavlink::UnpackLinkName(d.link_name_lo, d.link_name_hi), "radio_b");
  EXPECT_TRUE(d.ready);
  EXPECT_EQ(d.age_ms, 250U);
  EXPECT_EQ(d.valid, 3);
  EXPECT_EQ(d.rssi_dbm, -71);
  EXPECT_EQ(d.noise_floor_dbm, -110);
  EXPECT_EQ(d.snr_db, 39);
  EXPECT_EQ(d.rx_ok, 11U);
  EXPECT_EQ(d.tx_expired, 17U);
  EXPECT_EQ(d.budget_pct, 18);
}

TEST(LinkServiceMapping, GeneratedWireCodecRoundTripsTheStructure) {
  const auto in = ToServiceData(Sample());
  const auto wire = srp::data::Convert2Vector<LinkStatsData>::Conv(in);
  const auto out = srp::data::Convert<LinkStatsData>::Conv(wire);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->link_index, in.link_index);
  EXPECT_EQ(out->link_name_lo, in.link_name_lo);
  EXPECT_EQ(out->link_name_hi, in.link_name_hi);
  EXPECT_EQ(out->ready, in.ready);
  EXPECT_EQ(out->age_ms, in.age_ms);
  EXPECT_EQ(out->valid, in.valid);
  EXPECT_EQ(out->rssi_dbm, -71);
  EXPECT_EQ(out->noise_floor_dbm, -110);
  EXPECT_EQ(out->snr_db, 39);
  EXPECT_EQ(out->rx_ok, in.rx_ok);
  EXPECT_EQ(out->rx_crc_err, in.rx_crc_err);
  EXPECT_EQ(out->rx_sig_err, in.rx_sig_err);
  EXPECT_EQ(out->tx_frames, in.tx_frames);
  EXPECT_EQ(out->tx_busy, in.tx_busy);
  EXPECT_EQ(out->tx_dropped, in.tx_dropped);
  EXPECT_EQ(out->tx_expired, in.tx_expired);
  EXPECT_EQ(out->budget_pct, in.budget_pct);
}

TEST(LinkServiceMethod, GetLinkStatsAnswersAKnownLinkAndFailsForAnUnknownOne) {
  TestableService svc{ara::core::InstanceSpecifier{"srp/platform/com/mavlink_daemon/LinkServicePPort"},
                      [](const std::string& link) -> std::optional<dp::LinkStats> {
                        if (link == "radio_b") return Sample();
                        return std::nullopt;
                      }};
  svc.SetCoreRunning(true);
  LinkNameData known{};
  srp::platform::mavlink::PackLinkName("radio_b", &known.link_name_lo, &known.link_name_hi);
  const auto ok = svc.GetLinkStats(known);
  ASSERT_TRUE(ok.HasValue());
  EXPECT_EQ(ok.Value().rssi_dbm, -71);
  EXPECT_EQ(srp::platform::mavlink::UnpackLinkName(ok.Value().link_name_lo, ok.Value().link_name_hi), "radio_b");

  LinkNameData unknown{};
  srp::platform::mavlink::PackLinkName("nie_ma", &unknown.link_name_lo, &unknown.link_name_hi);
  EXPECT_FALSE(svc.GetLinkStats(unknown).HasValue());
}

TEST(LinkServiceMethod, GetLinkStatsDoesNotTouchTheCoreWhileItIsNotRunning) {
  std::atomic<int> calls{0};
  TestableService svc{ara::core::InstanceSpecifier{"srp/platform/com/mavlink_daemon/LinkServicePPort"},
                      [&](const std::string&) -> std::optional<dp::LinkStats> {
                        ++calls;
                        return Sample();
                      }};
  LinkNameData link{};
  srp::platform::mavlink::PackLinkName("radio_b", &link.link_name_lo, &link.link_name_hi);
  EXPECT_FALSE(svc.GetLinkStats(link).HasValue()) << "before the core started";
  EXPECT_EQ(calls.load(), 0);
  svc.SetCoreRunning(true);
  EXPECT_TRUE(svc.GetLinkStats(link).HasValue());
  EXPECT_EQ(calls.load(), 1);
  svc.SetCoreRunning(false);
  EXPECT_FALSE(svc.GetLinkStats(link).HasValue()) << "after the core was stopped";
  EXPECT_EQ(calls.load(), 1);
}

TEST(LinkServiceMethod, SetCoreRunningFalseWaitsForACallThatReadsTheCore) {
  // The "core" is valid only while `alive`; the service must never call it after SetCoreRunning(false)
  // returned (TSan / the assertion catch a use after Stop()).
  std::atomic<bool> alive{false};
  std::atomic<int> violations{0};
  TestableService svc{ara::core::InstanceSpecifier{"srp/platform/com/mavlink_daemon/LinkServicePPort"},
                      [&](const std::string&) -> std::optional<dp::LinkStats> {
                        if (!alive.load()) ++violations;
                        std::this_thread::yield();
                        return Sample();
                      }};
  LinkNameData link{};
  srp::platform::mavlink::PackLinkName("radio_b", &link.link_name_lo, &link.link_name_hi);
  std::atomic<bool> stop{false};
  std::thread caller{[&] {
    while (!stop.load()) std::ignore = svc.GetLinkStats(link);
  }};
  for (int i = 0; i < 200; ++i) {
    alive = true;
    svc.SetCoreRunning(true);
    std::this_thread::yield();
    svc.SetCoreRunning(false);
    alive = false;  // "Stop()": nothing may use the core from here on
  }
  stop = true;
  caller.join();
  EXPECT_EQ(violations.load(), 0);
}
}  // namespace
}  // namespace mavlink_daemon
}  // namespace srp
