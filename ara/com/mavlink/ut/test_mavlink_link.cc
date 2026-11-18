/**
 * Copyright (c) 2024
 *
 * @file test_mavlink_link.cc
 * @brief MavlinkLink na transporcie w pamięci: stany, retry, HEARTBEAT, kolejki,
 * koalescencja eventów, limit częstotliwości, R5, podpis.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>  // NOLINT
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/com_error_domain.h"
#include "ara/com/mavlink/mavlink_link.h"
#include "ara/com/mavlink/testing/memory_transport.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::MemoryTransport;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr std::uint8_t kExtra = 42;

std::string TmpPath(const std::string& name) {
  const char* d = std::getenv("TEST_TMPDIR");
  return std::string(d != nullptr ? d : "/tmp") + "/" + name;
}

std::optional<std::uint8_t> Lookup(std::uint32_t id) {
  if (id == 200 || id == 201 || id == 202) {
    return kExtra;
  }
  return std::nullopt;
}

OutMessage Msg(std::uint32_t id, std::uint8_t first, std::size_t len = 4) {
  OutMessage m;
  m.msg_id = id;
  m.crc_extra = kExtra;
  m.payload.assign(len, 0);
  m.payload[0] = first;
  return m;
}

/// Payload bez końcowych zer (v2 niczego nie obcina): rozmiar ramki jest przewidywalny.
OutMessage Full(std::uint32_t id, std::uint8_t first, std::size_t len) {
  OutMessage m;
  m.msg_id = id;
  m.crc_extra = kExtra;
  m.payload.assign(len, 0x5A);
  m.payload[0] = first;
  return m;
}

struct Logs {
  std::mutex mu;
  std::vector<std::pair<LogLevel, std::string>> items;
  void Add(LogLevel l, const std::string& s) {
    std::lock_guard<std::mutex> lock{mu};
    items.emplace_back(l, s);
  }
  std::string Dump() {
    std::lock_guard<std::mutex> lock{mu};
    std::string o;
    for (const auto& item : items) {
      o += "\n  [" + std::to_string(static_cast<int>(item.first)) + "] " + item.second;
    }
    return o;
  }
  int Count(LogLevel l, const std::string& needle = "") {
    std::lock_guard<std::mutex> lock{mu};
    int n = 0;
    for (const auto& [lv, s] : items) {
      if (lv == l && s.find(needle) != std::string::npos) ++n;
    }
    return n;
  }
};

/// Strona "radia": czyta ramki, które wysłało łącze.
struct Peer {
  std::shared_ptr<MemoryTransport> tr;
  FrameParser parser{[](std::uint32_t id) -> std::optional<std::uint8_t> {
    if (id == 0) return 50;
    return Lookup(id);
  }};
  std::vector<Frame> frames;

  void Pump(std::uint32_t ms) {
    const auto end = Clock::now() + milliseconds(ms);
    std::uint8_t buf[512];
    while (Clock::now() < end) {
      const auto r = tr->Read(std::span<std::uint8_t>{buf, sizeof(buf)}, 20, {});
      if (r.HasValue() && r.Value() > 0) {
        parser.Feed(std::span<const std::uint8_t>{buf, r.Value()}, &frames);
      }
    }
  }
  std::size_t Count(std::uint32_t msgid) const {
    std::size_t n = 0;
    for (const auto& f : frames) n += f.msgid == msgid ? 1 : 0;
    return n;
  }
  void Send(const std::vector<std::uint8_t>& bytes) {
    ASSERT_TRUE(tr->Write(std::span<const std::uint8_t>{bytes}, 1000, {}).HasValue());
  }
};

class LinkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto pair = MemoryTransport::MakePair("link", "radio", 64 * 1024);
    transport_ = pair.first;
    peer_.tr = pair.second;
    ASSERT_TRUE(peer_.tr->Open({}).HasValue());
    cfg_.name = "t";
    cfg_.heartbeat_hz = 0;
    cfg_.retry_interval = milliseconds(40);
    cfg_.read_timeout_ms = 20;
    cfg_.write_timeout_ms = 100;
  }
  void TearDown() override {
    if (link_) link_->Stop();
  }

  void Make(const MavlinkLink::TransportProvider* custom = nullptr) {
    MavlinkLink::TransportProvider prov =
        custom != nullptr ? *custom
                          : MavlinkLink::TransportProvider{
                                [this](milliseconds, std::stop_token) { return transport_; }};
    link_ = std::make_unique<MavlinkLink>(
        cfg_, prov, Lookup,
        [this](const std::string&, const Frame& f) {
          std::lock_guard<std::mutex> lock{rx_mu_};
          rx_.push_back(f);
        },
        [this](LogLevel l, const std::string& s) { logs_.Add(l, s); }, [this] { return now_; });
  }
  std::size_t RxCount() {
    std::lock_guard<std::mutex> lock{rx_mu_};
    return rx_.size();
  }
  bool WaitRx(std::size_t n, std::uint32_t ms = 2000) {
    const auto end = Clock::now() + milliseconds(ms);
    while (Clock::now() < end) {
      if (RxCount() >= n) return true;
      std::this_thread::sleep_for(milliseconds(5));
    }
    return RxCount() >= n;
  }
  void WriteKey(const std::string& path, std::uint8_t seed) {
    std::ofstream out{path, std::ios::trunc};
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
      const std::uint8_t b = static_cast<std::uint8_t>(seed + i);
      out << hex[b >> 4] << hex[b & 15];
    }
    out << "\n";
  }

  LinkConfig cfg_;
  std::shared_ptr<MemoryTransport> transport_;
  Peer peer_;
  std::unique_ptr<MavlinkLink> link_;
  Logs logs_;
  std::mutex rx_mu_;
  std::vector<Frame> rx_;
  std::uint64_t now_{kClockSanityThreshold + 1000};
};

// ---------------------------------------------------------------- stany

TEST_F(LinkTest, ReachesRunningAndStops) {
  Make();
  EXPECT_FALSE(link_->IsReady());
  link_->Start();
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  EXPECT_TRUE(link_->IsReady());
  const auto t0 = Clock::now();
  link_->Stop();
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 1000);
  EXPECT_EQ(link_->state(), LinkState::kStopped);
  EXPECT_FALSE(link_->IsReady());
}

TEST_F(LinkTest, StartTwiceIsHarmless) {
  Make();
  link_->Start();
  link_->Start();
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  EXPECT_EQ(link_->counters().opens, 1U);
}

TEST_F(LinkTest, OpenErrorsAreRetriedAndCounted) {
  transport_->FailNextOpens(2);
  Make();
  link_->Start();
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(2000)));
  EXPECT_EQ(link_->counters().open_errors, 2U);
  EXPECT_EQ(link_->counters().opens, 3U);
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "Open() nie powiodło"), 2);
}

TEST_F(LinkTest, PersistentOpenErrorLoggedOnceAsError) {
  transport_->FailNextOpens(4, /*persistent=*/true);
  Make();
  link_->Start();
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(3000)));
  EXPECT_EQ(link_->counters().open_errors, 4U);
  EXPECT_EQ(logs_.Count(LogLevel::kError, "trwały"), 1);  // Error raz mimo 4 prób
}

TEST_F(LinkTest, OpenErrorStateVisibleWhileFailing) {
  transport_->FailNextOpens(1000);
  cfg_.retry_interval = milliseconds(200);
  Make();
  link_->Start();
  EXPECT_TRUE(link_->WaitForState(LinkState::kOpenError, milliseconds(1000)));
  EXPECT_FALSE(link_->IsReady());
}

TEST_F(LinkTest, StopWhileOpenIsBlockedReturnsPromptly) {
  transport_->SetOpenDelayMs(5000);
  Make();
  link_->Start();
  std::this_thread::sleep_for(milliseconds(100));
  const auto t0 = Clock::now();
  link_->Stop();
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 1500);
}

TEST_F(LinkTest, ExternalWithoutTransportWaitsThenStarts) {
  std::atomic<bool> available{false};
  cfg_.transport_warn_after = milliseconds(100);
  MavlinkLink::TransportProvider prov = [&](milliseconds wait, std::stop_token st) {
    if (available.load()) return std::shared_ptr<IByteTransport>{transport_};
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> lock{m};
    cv.wait_for(lock, st, wait, [] { return false; });
    return std::shared_ptr<IByteTransport>{};
  };
  Make(&prov);
  link_->Start();
  std::this_thread::sleep_for(milliseconds(400));
  EXPECT_EQ(link_->state(), LinkState::kWaitingForTransport);
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "RegisterLinkTransport"), 1);  // raz
  available = true;
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(2000)));
  // ruch czekał w kolejce i idzie po rejestracji
}

TEST_F(LinkTest, TrafficQueuedBeforeTransportIsSentAfterwards) {
  std::atomic<bool> available{false};
  MavlinkLink::TransportProvider prov = [&](milliseconds wait, std::stop_token st) {
    if (available.load()) return std::shared_ptr<IByteTransport>{transport_};
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> lock{m};
    cv.wait_for(lock, st, wait, [] { return false; });
    return std::shared_ptr<IByteTransport>{};
  };
  Make(&prov);
  link_->Start();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)).HasValue());
  link_->EnqueueEvent(1, Msg(201, 2), milliseconds(0));
  peer_.Pump(200);
  EXPECT_EQ(peer_.frames.size(), 0U);
  available = true;
  peer_.Pump(500);
  EXPECT_EQ(peer_.Count(200), 1U);
  EXPECT_EQ(peer_.Count(201), 1U);
}

TEST_F(LinkTest, WriteErrorTriggersReconnect) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  transport_->SetFailIo(true);
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)).HasValue());
  EXPECT_TRUE(link_->WaitForState(LinkState::kOpenError, milliseconds(2000)));
  transport_->SetFailIo(false);
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(3000)));
  EXPECT_GE(link_->counters().opens, 2U);
}

TEST_F(LinkTest, ExternalCloseGoesBackToWaitingWithoutRetryDelay) {
  std::atomic<bool> available{true};
  MavlinkLink::TransportProvider prov = [&](milliseconds wait, std::stop_token st) {
    if (available.load()) return std::shared_ptr<IByteTransport>{transport_};
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> lock{m};
    cv.wait_for(lock, st, wait, [] { return false; });
    return std::shared_ptr<IByteTransport>{};
  };
  cfg_.retry_interval = milliseconds(10000);  // gdyby zadziałał retry, test by się zawiesił
  Make(&prov);
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  available = false;
  transport_->Close();  // jak UnregisterLinkTransport()
  EXPECT_TRUE(link_->WaitForState(LinkState::kWaitingForTransport, milliseconds(1500)));
  available = true;
  EXPECT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1500)));
}

// ---------------------------------------------------------------- HEARTBEAT

TEST_F(LinkTest, HeartbeatIsSentWithOnboardControllerAndInvalidAutopilot) {
  cfg_.heartbeat_hz = 50;
  Make();
  link_->Start();
  peer_.Pump(400);
  ASSERT_GE(peer_.Count(0), 5U);
  const auto& hb = peer_.frames[0];
  ASSERT_EQ(hb.payload.size(), 9U);
  EXPECT_EQ(hb.payload[4], 18);  // MAV_TYPE_ONBOARD_CONTROLLER
  EXPECT_EQ(hb.payload[5], 8);   // MAV_AUTOPILOT_INVALID
  EXPECT_GE(link_->counters().heartbeats_tx, 5U);
}

TEST_F(LinkTest, HeartbeatDisabledSendsNothing) {
  cfg_.heartbeat_hz = 0;
  Make();
  link_->Start();
  peer_.Pump(300);
  EXPECT_EQ(peer_.Count(0), 0U);
}

TEST_F(LinkTest, HeartbeatRateRoughlyMatchesConfig) {
  cfg_.heartbeat_hz = 20;
  Make();
  link_->Start();
  peer_.Pump(1000);
  EXPECT_GE(peer_.Count(0), 14U);
  EXPECT_LE(peer_.Count(0), 26U);
}

TEST_F(LinkTest, HeartbeatUsesConfiguredSystemAndComponentIds) {
  cfg_.heartbeat_hz = 50;
  cfg_.system_id = 7;
  cfg_.component_id = 9;
  Make();
  link_->Start();
  peer_.Pump(150);
  ASSERT_GE(peer_.frames.size(), 1U);
  EXPECT_EQ(peer_.frames[0].sysid, 7);
  EXPECT_EQ(peer_.frames[0].compid, 9);
}

TEST_F(LinkTest, ReceivedHeartbeatsAreCountedNotDelivered) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  peer_.Send(BuildFrame(2, 0, 5, 5, 0, std::vector<std::uint8_t>{0, 0, 0, 0, 6, 8, 0, 4, 3}, 50));
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(link_->counters().heartbeats_rx, 1U);
  EXPECT_EQ(RxCount(), 0U);
}

// ---------------------------------------------------------------- RX

TEST_F(LinkTest, ReceivedFrameReachesHandler) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  peer_.Send(BuildFrame(2, 3, 5, 6, 200, std::vector<std::uint8_t>{9, 8, 7}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  EXPECT_EQ(rx_[0].sysid, 5);
  EXPECT_EQ(rx_[0].compid, 6);
  EXPECT_EQ(rx_[0].msgid, 200U);
  EXPECT_EQ(link_->counters().rx_ok, 1U);
}

TEST_F(LinkTest, V1AndV2FramesAreBothAccepted) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  peer_.Send(BuildFrame(1, 1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra));
  peer_.Send(BuildFrame(2, 2, 5, 6, 201, std::vector<std::uint8_t>{2}, kExtra));
  ASSERT_TRUE(WaitRx(2));
  EXPECT_EQ(rx_[0].version, 1);
  EXPECT_EQ(rx_[1].version, 2);
}

TEST_F(LinkTest, CrcErrorsAndUnknownMessagesAreCounted) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  auto bad = BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3}, kExtra);
  bad[10] ^= 0x01;
  peer_.Send(bad);
  peer_.Send(BuildFrame(2, 2, 5, 6, 999, std::vector<std::uint8_t>{1}, kExtra));
  peer_.Send(BuildFrame(2, 3, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  EXPECT_GE(link_->counters().rx_crc_err, 1U);
  EXPECT_EQ(link_->counters().rx_dropped, 1U);
  EXPECT_EQ(link_->counters().rx_ok, 1U);
}

TEST_F(LinkTest, GarbageAndFalseStartsDoNotLoseFollowingFrames) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  std::vector<std::uint8_t> stream{0x00, 0xFD, 0x05, 0xFE, 0x77};
  const auto good = BuildFrame(2, 9, 5, 6, 200, std::vector<std::uint8_t>{4, 5}, kExtra);
  stream.insert(stream.end(), good.begin(), good.end());
  stream.insert(stream.end(), good.begin(), good.end());
  peer_.Send(stream);
  ASSERT_TRUE(WaitRx(2));
}

// ---------------------------------------------------------------- TX: kolejki

TEST_F(LinkTest, RequestsAreSentInFifoOrder) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  for (std::uint8_t i = 1; i <= 10; ++i) {
    ASSERT_TRUE(link_->EnqueueRequest(Msg(200, i)).HasValue());
  }
  peer_.Pump(300);
  ASSERT_EQ(peer_.Count(200), 10U);
  for (std::uint8_t i = 0; i < 10; ++i) {
    EXPECT_EQ(peer_.frames[i].payload[0], i + 1);
  }
}

TEST_F(LinkTest, FifoLimitRejectsRequestImmediatelyAndDropsResponse) {
  cfg_.fifo_limit = 3;
  Make();  // link niestartowany: kolejka rośnie
  EXPECT_TRUE(link_->EnqueueRequest(Msg(200, 1)).HasValue());
  EXPECT_TRUE(link_->EnqueueRequest(Msg(200, 2)).HasValue());
  EXPECT_TRUE(link_->EnqueueResponse(Msg(201, 3)));
  const auto full = link_->EnqueueRequest(Msg(200, 4));
  ASSERT_FALSE(full.HasValue());
  EXPECT_EQ(full.Error().Value(),
            static_cast<ara::core::ErrorDomain::CodeType>(ara::com::ComErrc::kCommunicationLinkError));
  EXPECT_FALSE(link_->EnqueueResponse(Msg(201, 5)));
  EXPECT_EQ(link_->counters().tx_dropped, 1U);
}

TEST_F(LinkTest, EventsCoalesceToLatestValuePerKey) {
  Make();
  for (std::uint8_t i = 1; i <= 20; ++i) {
    link_->EnqueueEvent(7, Msg(201, i), milliseconds(0));
  }
  link_->EnqueueEvent(8, Msg(202, 99), milliseconds(0));
  link_->Start();
  peer_.Pump(300);
  EXPECT_EQ(peer_.Count(201), 1U);
  EXPECT_EQ(peer_.Count(202), 1U);
  for (const auto& f : peer_.frames) {
    if (f.msgid == 201) {
      EXPECT_EQ(f.payload[0], 20);
    }
  }
}

TEST_F(LinkTest, RequestsGoBeforeEvents) {
  Make();
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(0));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 2)).HasValue());
  link_->Start();
  peer_.Pump(300);
  ASSERT_EQ(peer_.frames.size(), 2U);
  EXPECT_EQ(peer_.frames[0].msgid, 200U);
}

TEST_F(LinkTest, EventRateLimitSendsAtMostConfiguredFrequency) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  const auto t0 = Clock::now();
  std::uint8_t v = 0;
  while (Clock::now() - t0 < milliseconds(1000)) {
    link_->EnqueueEvent(1, Msg(201, ++v), milliseconds(100));  // 10 Hz
    std::this_thread::sleep_for(milliseconds(10));
  }
  peer_.Pump(200);
  // 10 Hz przez ~1 s, tolerancja ±20% (+1 na pierwszą natychmiast)
  EXPECT_GE(peer_.Count(201), 8U);
  EXPECT_LE(peer_.Count(201), 13U);
}

TEST_F(LinkTest, RateLimitedEventSendsLatestValueInNextWindowNotAQueue) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(300));
  peer_.Pump(60);
  for (std::uint8_t i = 2; i <= 9; ++i) {
    link_->EnqueueEvent(1, Msg(201, i), milliseconds(300));
  }
  peer_.Pump(500);
  ASSERT_EQ(peer_.Count(201), 2U);  // pierwszy + ostatnia wartość, bez 2..8
  EXPECT_EQ(peer_.frames.back().payload[0], 9);
}

TEST_F(LinkTest, ZeroIntervalMeansNoLimit) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  for (int i = 0; i < 30; ++i) {
    link_->EnqueueEvent(1, Msg(201, static_cast<std::uint8_t>(i)), milliseconds(0));
    peer_.Pump(15);
  }
  EXPECT_GE(peer_.Count(201), 25U);
}

TEST_F(LinkTest, TxSequenceNumberIncrementsAndWraps) {
  cfg_.fifo_limit = 1000;
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  for (int i = 0; i < 300; ++i) {
    ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1, 1)).HasValue());
  }
  peer_.Pump(800);
  ASSERT_EQ(peer_.Count(200), 300U);
  for (std::size_t i = 0; i < 300; ++i) {
    EXPECT_EQ(peer_.frames[i].seq, static_cast<std::uint8_t>(i));
  }
}

TEST_F(LinkTest, VersionOneLinkSendsV1Frames) {
  cfg_.version = 1;
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 5)).HasValue());
  peer_.Pump(200);
  ASSERT_EQ(peer_.Count(200), 1U);
  EXPECT_EQ(peer_.frames[0].version, 1);
  EXPECT_EQ(peer_.frames[0].raw[0], 0xFE);
}

TEST_F(LinkTest, VersionTwoLinkSendsV2FramesWithTruncation) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 5, 10)).HasValue());
  peer_.Pump(200);
  ASSERT_EQ(peer_.Count(200), 1U);
  EXPECT_EQ(peer_.frames[0].version, 2);
  EXPECT_EQ(peer_.frames[0].payload.size(), 1U);  // 9 końcowych zer obciętych
}

// ---------------------------------------------------------------- R5

TEST_F(LinkTest, WriteTimeoutInTheMiddleOfFrameDropsRestAndNextFrameIsWhole) {
  transport_->SetTxCapacity(20);  // połowa ramki mieści się, reszta czeka
  cfg_.write_timeout_ms = 80;
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  ASSERT_TRUE(link_->EnqueueRequest(Full(200, 1, 30)).HasValue());  // ramka 12+30+2 > 20
  std::this_thread::sleep_for(milliseconds(400));
  EXPECT_EQ(link_->counters().tx_partial, 1U);
  EXPECT_EQ(link_->counters().tx, 0U);
  EXPECT_EQ(link_->state(), LinkState::kRunning);  // to nie jest błąd łącza
  transport_->SetTxCapacity(64 * 1024);
  ASSERT_TRUE(link_->EnqueueRequest(Msg(201, 7, 3)).HasValue());
  peer_.Pump(400);
  EXPECT_EQ(link_->counters().tx, 1U);
  // peer widzi: urwaną ramkę (odrzuconą przez CRC / resync) i całą następną
  ASSERT_EQ(peer_.Count(201), 1U);
  EXPECT_EQ(peer_.Count(200), 0U);
}

TEST_F(LinkTest, PartialShortWritesWithoutTimeoutAreCompleted) {
  // Filtr zapisu skraca pojedyncze zapisy? Transport zwraca pełne ilości, więc
  // sprawdzamy tylko brak fałszywych tx_partial przy normalnym ruchu.
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1, 40)).HasValue());
  }
  peer_.Pump(300);
  EXPECT_EQ(link_->counters().tx_partial, 0U);
  EXPECT_EQ(peer_.Count(200), 20U);
}

// ---------------------------------------------------------------- podpis

TEST_F(LinkTest, SignedTxIsVerifiableAndMonotonic) {
  const auto key = TmpPath("link_key_a");
  WriteKey(key, 3);
  cfg_.signing_enabled = true;
  cfg_.key_file = key;
  cfg_.signing_link_id = 4;
  cfg_.fifo_limit = 100;
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i))).HasValue());
  }
  peer_.Pump(300);
  ASSERT_EQ(peer_.Count(200), 5U);
  LinkSigner::Config sc;
  for (int i = 0; i < 32; ++i) sc.key[i] = static_cast<std::uint8_t>(3 + i);
  sc.link_id = 1;
  LinkSigner verifier{sc, [this] { return now_; }};
  std::uint64_t last = 0;
  for (const auto& f : peer_.frames) {
    EXPECT_TRUE(f.has_signature);
    EXPECT_EQ(f.link_id, 4);
    EXPECT_GT(f.timestamp, last);
    last = f.timestamp;
    EXPECT_EQ(verifier.Verify(f), SignatureVerdict::kOk);
  }
  std::remove(key.c_str());
}

TEST_F(LinkTest, WrongKeyFileGivesInitErrorAndNothingIsSent) {
  cfg_.signing_enabled = true;
  cfg_.key_file = TmpPath("brak_klucza_xyz");
  cfg_.heartbeat_hz = 50;
  Make();
  link_->Start();
  EXPECT_EQ(link_->state(), LinkState::kInitError);
  std::ignore = link_->EnqueueRequest(Msg(200, 1));  // kolejka przyjmuje, ale nic nie wychodzi
  peer_.Pump(300);
  EXPECT_EQ(peer_.frames.size(), 0U);
  EXPECT_EQ(logs_.Count(LogLevel::kError, "błąd inicjalizacji podpisu"), 1);
}

TEST_F(LinkTest, ShortKeyFileGivesInitError) {
  const auto key = TmpPath("link_key_short");
  std::ofstream{key} << "abcd\n";
  cfg_.signing_enabled = true;
  cfg_.key_file = key;
  Make();
  link_->Start();
  EXPECT_EQ(link_->state(), LinkState::kInitError);
  std::remove(key.c_str());
}

TEST_F(LinkTest, RxRejectsBadSignatureReplayAndUnsigned) {
  const auto key = TmpPath("link_key_rx");
  WriteKey(key, 10);
  cfg_.signing_enabled = true;
  cfg_.key_file = key;
  cfg_.accept_unsigned = false;
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  std::array<std::uint8_t, 32> good{};
  std::array<std::uint8_t, 32> bad{};
  for (int i = 0; i < 32; ++i) {
    good[i] = static_cast<std::uint8_t>(10 + i);
    bad[i] = static_cast<std::uint8_t>(99 + i);
  }
  const std::vector<std::uint8_t> payload{1, 2, 3};
  const SigningParams ok1{good, 5, 1000};
  const SigningParams wrongkey{bad, 5, 2000};
  const SigningParams replay{good, 5, 1000};
  const SigningParams ok2{good, 5, 1001};
  peer_.Send(BuildFrame(2, 1, 5, 6, 200, payload, kExtra, &ok1));        // OK
  peer_.Send(BuildFrame(2, 2, 5, 6, 200, payload, kExtra, &wrongkey));   // zły klucz
  peer_.Send(BuildFrame(2, 3, 5, 6, 200, payload, kExtra, &replay));     // replay
  peer_.Send(BuildFrame(2, 4, 5, 6, 200, payload, kExtra));              // niepodpisana
  peer_.Send(BuildFrame(2, 5, 5, 6, 200, payload, kExtra, &ok2));        // OK
  ASSERT_TRUE(WaitRx(2));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(RxCount(), 2U);
  EXPECT_EQ(link_->counters().rx_ok, 2U);
  EXPECT_EQ(link_->counters().rx_sig_err, 3U);
  std::remove(key.c_str());
}

TEST_F(LinkTest, AcceptUnsignedLetsUnsignedFramesThrough) {
  const auto key = TmpPath("link_key_lax");
  WriteKey(key, 10);
  cfg_.signing_enabled = true;
  cfg_.key_file = key;
  cfg_.accept_unsigned = true;
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  peer_.Send(BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  EXPECT_EQ(link_->counters().rx_sig_err, 0U);
  std::remove(key.c_str());
}

TEST_F(LinkTest, SigningDisabledIgnoresSignatureOfIncomingFrames) {
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  std::array<std::uint8_t, 32> k{};
  const SigningParams sp{k, 1, 5};
  peer_.Send(BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra, &sp));
  ASSERT_TRUE(WaitRx(1));
}

TEST_F(LinkTest, SigningStatePersistedAndUsedAsFloorAfterRestart) {
  const auto key = TmpPath("link_key_p");
  const auto state = TmpPath("link_state_p");
  std::remove(state.c_str());
  WriteKey(key, 3);
  cfg_.signing_enabled = true;
  cfg_.key_file = key;
  cfg_.state_file = state;
  cfg_.fifo_limit = 100;
  now_ = 0;  // BBB bez RTC
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)).HasValue());
  peer_.Pump(300);
  const std::uint64_t last_before = peer_.frames.back().timestamp;
  link_->Stop();
  {
    std::ofstream out{state, std::ios::trunc};
    out << last_before << "\n";  // jak PersistIfDue
  }
  peer_.frames.clear();
  auto pair2 = MemoryTransport::MakePair("link2", "radio2", 64 * 1024);
  transport_ = pair2.first;
  peer_.tr = pair2.second;
  ASSERT_TRUE(peer_.tr->Open({}).HasValue());
  Make();
  link_->Start();
  ASSERT_TRUE(link_->WaitForState(LinkState::kRunning, milliseconds(1000)));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)).HasValue());
  peer_.Pump(300);
  ASSERT_EQ(peer_.Count(200), 1U) << "state=" << static_cast<int>(link_->state())
                                  << " tx=" << link_->counters().tx
                                  << " opens=" << link_->counters().opens
                                  << " drop=" << link_->counters().tx_dropped
                                  << " partial=" << link_->counters().tx_partial
                                  << " frames=" << peer_.frames.size()
                                  << " logs=" << logs_.Dump();
  EXPECT_GT(peer_.frames[0].timestamp, last_before + kStartupFloor - 1);
  std::remove(key.c_str());
  std::remove(state.c_str());
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
