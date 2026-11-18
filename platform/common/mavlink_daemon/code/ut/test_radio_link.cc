/**
 * Copyright (c) 2024
 *
 * @file test_radio_link.cc
 * @brief RadioLink on a MemoryRadio: states and driver restart, HEARTBEAT, queues (FIFO, coalescing,
 * rate limit), kBusy/OnTxReady without loss or duplicate, v2 signing (TX/RX/replay/persist).
 */
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>  // NOLINT
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "ara/com/mavlink/mavlink_frame.h"
#include "ara/com/mavlink/mavlink_signing.h"
#include "ara/com/mavlink/testing/memory_radio.h"
#include "platform/common/mavlink_daemon/code/link/radio_link.h"

namespace srp {
namespace mavlink_daemon {
namespace {
using ara::com::mavlink::BuildFrame;
using ara::com::mavlink::Frame;
using ara::com::mavlink::FrameParser;
using ara::com::mavlink::LinkSigner;
using ara::com::mavlink::LogLevel;
using ara::com::mavlink::SignatureVerdict;
using ara::com::mavlink::SigningParams;
using ara::com::mavlink::testing::MemoryRadio;
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

OutFrame Msg(std::uint32_t id, std::uint8_t first, std::size_t len = 4) {
  OutFrame m;
  m.msg_id = id;
  m.crc_extra = kExtra;
  m.payload.assign(len, 0);
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

class LinkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    radio_ = std::make_shared<MemoryRadio>("radio");
    cfg_.link.name = "t";
    cfg_.link.heartbeat_hz = 0;
    cfg_.restart_delay = milliseconds(60);
  }
  void TearDown() override {
    if (link_) link_->Stop();
  }

  void Make() {
    link_ = std::make_unique<RadioLink>(
        cfg_, radio_, Lookup,
        [this](const std::string&, const Frame& f) {
          std::lock_guard<std::mutex> lock{rx_mu_};
          rx_.push_back(f);
          return accept_.load();
        },
        [this](const std::string&, bool ready) {
          std::lock_guard<std::mutex> lock{rx_mu_};
          ready_events_.push_back(ready);
        },
        [this](LogLevel l, const std::string& s) { logs_.Add(l, s); }, [this] { return now_; });
  }
  bool WaitState(RadioLinkState st, milliseconds timeout) {
    const auto end = Clock::now() + timeout;
    while (Clock::now() < end) {
      if (link_->state() == st) return true;
      std::this_thread::sleep_for(milliseconds(2));
    }
    return link_->state() == st;
  }
  void StartRunning() {
    Make();
    link_->Start();
    ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(2000)));
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
  std::vector<bool> ReadyEvents() {
    std::lock_guard<std::mutex> lock{rx_mu_};
    return ready_events_;
  }
  /// Frames sent by the link (parsed).
  std::vector<Frame> Sent() {
    std::vector<Frame> out;
    FrameParser parser{[](std::uint32_t id) -> std::optional<std::uint8_t> {
      if (id == 0) return 50;
      return Lookup(id);
    }};
    for (const auto& bytes : radio_->sent()) {
      parser.Feed(bytes, &out);
    }
    return out;
  }
  std::size_t SentCount(std::uint32_t msgid) {
    std::size_t n = 0;
    for (const auto& f : Sent()) n += f.msgid == msgid ? 1 : 0;
    return n;
  }
  /// Waits for `n` frames with the given msgid.
  bool WaitSentCount(std::uint32_t msgid, std::size_t n, std::uint32_t ms = 2000) {
    const auto end = Clock::now() + milliseconds(ms);
    while (Clock::now() < end) {
      if (SentCount(msgid) >= n) return true;
      std::this_thread::sleep_for(milliseconds(5));
    }
    return SentCount(msgid) >= n;
  }
  void Inject(const std::vector<std::uint8_t>& bytes) { radio_->Inject(bytes); }
  void WriteKey(const std::string& path, std::uint8_t seed) {
    std::ofstream out{path, std::ios::trunc};
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
      const std::uint8_t b = static_cast<std::uint8_t>(seed + i);
      out << hex[b >> 4] << hex[b & 15];
    }
    out << "\n";
  }

  RadioLinkConfig cfg_;
  std::shared_ptr<MemoryRadio> radio_;
  std::unique_ptr<RadioLink> link_;
  Logs logs_;
  std::mutex rx_mu_;
  std::vector<Frame> rx_;
  std::vector<bool> ready_events_;
  std::atomic<bool> accept_{true};  // the result the frame handler reports to the link
  std::uint64_t now_{ara::com::mavlink::kClockSanityThreshold + 1000};
};

// ---------------------------------------------------------------- states / driver restart

TEST_F(LinkTest, ReachesRunningAndStops) {
  StartRunning();
  EXPECT_TRUE(link_->IsReady());
  EXPECT_EQ(radio_->start_count(), 1);
  link_->Stop();
  EXPECT_EQ(link_->state(), RadioLinkState::kStopped);
  EXPECT_FALSE(link_->IsReady());
  EXPECT_EQ(radio_->stop_count(), 1);
  EXPECT_EQ(ReadyEvents(), (std::vector<bool>{true, false}));
}

TEST_F(LinkTest, StartTwiceIsHarmless) {
  StartRunning();
  link_->Start();
  EXPECT_EQ(link_->state(), RadioLinkState::kRunning);
  EXPECT_EQ(radio_->start_count(), 1);
}

TEST_F(LinkTest, StartErrorsAreRetriedAndCounted) {
  radio_->FailStarts(3, false);
  Make();
  link_->Start();
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  EXPECT_EQ(link_->counters().start_errors, 3U);
  EXPECT_EQ(radio_->start_attempts(), 4);
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "failed to start"), 3);
}

TEST_F(LinkTest, PersistentStartErrorLoggedOnceAsError) {
  radio_->FailStarts(4, true);
  Make();
  link_->Start();
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  EXPECT_EQ(logs_.Count(LogLevel::kError, "persistent"), 1) << logs_.Dump();
}

TEST_F(LinkTest, RestartWaitStateVisibleWhileStartFails) {
  cfg_.restart_delay = milliseconds(500);
  radio_->FailStarts(1000, true);
  Make();
  link_->Start();
  EXPECT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(1000)));
  EXPECT_FALSE(link_->IsReady());
}

TEST_F(LinkTest, StopWhileWaitingForRestartReturnsPromptly) {
  cfg_.restart_delay = milliseconds(60000);
  radio_->FailStarts(1000, true);
  Make();
  link_->Start();
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(1000)));
  const auto t0 = Clock::now();
  link_->Stop();
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 2000);
}

TEST_F(LinkTest, FatalErrorStopsDriverAndRestartsItAfterTheDelay) {
  cfg_.restart_delay = milliseconds(200);
  StartRunning();
  const auto t0 = Clock::now();
  radio_->Fatal("radio broke");
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(2000)));
  EXPECT_FALSE(link_->IsReady());
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  EXPECT_GE(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 190);
  EXPECT_EQ(radio_->start_count(), 2);
  EXPECT_EQ(radio_->stop_count(), 1);  // the driver gets Stop() before the next Start()
  EXPECT_EQ(link_->counters().fatal_errors, 1U);
  EXPECT_EQ(logs_.Count(LogLevel::kError, "radio broke"), 1);
  EXPECT_EQ(ReadyEvents(), (std::vector<bool>{true, false, true}));
}

TEST_F(LinkTest, NonFatalErrorIsLoggedAndLinkKeepsRunning) {
  StartRunning();
  radio_->NonFatal("warning");
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(link_->state(), RadioLinkState::kRunning);
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "warning"), 1);
  EXPECT_EQ(radio_->start_count(), 1);
}

TEST_F(LinkTest, TrafficEnqueuedBeforeDriverStartIsSentAfterwards) {
  radio_->FailStarts(2, false);
  Make();
  link_->Start();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 7)));
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  ASSERT_TRUE(WaitSentCount(200, 1));
}

// ---------------------------------------------------------------- HEARTBEAT

TEST_F(LinkTest, HeartbeatIsSentWithOnboardControllerAndInvalidAutopilot) {
  cfg_.link.heartbeat_hz = 50;
  StartRunning();
  ASSERT_TRUE(WaitSentCount(0, 3));
  const auto frames = Sent();
  const auto& hb = frames.at(0);
  ASSERT_EQ(hb.msgid, 0U);
  ASSERT_GE(hb.payload.size(), 8U);
  EXPECT_EQ(hb.payload[4], 18);  // MAV_TYPE_ONBOARD_CONTROLLER
  EXPECT_EQ(hb.payload[5], 8);   // MAV_AUTOPILOT_INVALID
}

TEST_F(LinkTest, HeartbeatDisabledSendsNothing) {
  StartRunning();
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_EQ(radio_->sent_count(), 0U);
}

TEST_F(LinkTest, HeartbeatRateRoughlyMatchesConfig) {
  cfg_.link.heartbeat_hz = 20;
  StartRunning();
  std::this_thread::sleep_for(milliseconds(1000));
  const auto n = SentCount(0);
  EXPECT_GE(n, 12U);
  EXPECT_LE(n, 26U);
}

TEST_F(LinkTest, HeartbeatUsesConfiguredSystemAndComponentIds) {
  cfg_.link.heartbeat_hz = 50;
  cfg_.system_id = 11;
  cfg_.component_id = 3;
  StartRunning();
  ASSERT_TRUE(WaitSentCount(0, 1));
  const auto f = Sent().at(0);
  EXPECT_EQ(f.sysid, 11);
  EXPECT_EQ(f.compid, 3);
}

TEST_F(LinkTest, ReceivedHeartbeatsAreCountedNotDelivered) {
  StartRunning();
  Inject(BuildFrame(2, 1, 5, 6, 0, std::vector<std::uint8_t>{0, 0, 0, 0, 1, 2, 3, 4, 3}, 50));
  const auto end = Clock::now() + milliseconds(2000);
  while (link_->counters().heartbeats_rx == 0 && Clock::now() < end) {
    std::this_thread::sleep_for(milliseconds(5));
  }
  EXPECT_EQ(link_->counters().heartbeats_rx, 1U);
  EXPECT_EQ(RxCount(), 0U);
}

// ---------------------------------------------------------------- RX

TEST_F(LinkTest, ReceivedFrameReachesHandlerEvenWhenSplitIntoSmallChunks) {
  StartRunning();
  const auto bytes = BuildFrame(2, 9, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  radio_->Inject(bytes, 3);  // 3 B chunks from the driver thread
  ASSERT_TRUE(WaitRx(1));
  std::lock_guard<std::mutex> lock{rx_mu_};
  EXPECT_EQ(rx_[0].msgid, 200U);
  EXPECT_EQ(rx_[0].sysid, 5);
  EXPECT_EQ(rx_[0].payload, (std::vector<std::uint8_t>{1, 2, 3, 4}));
}

TEST_F(LinkTest, V1AndV2FramesAreBothAccepted) {
  StartRunning();
  Inject(BuildFrame(1, 1, 5, 6, 200, std::vector<std::uint8_t>{1, 2}, kExtra));
  Inject(BuildFrame(2, 2, 5, 6, 201, std::vector<std::uint8_t>{3, 4}, kExtra));
  ASSERT_TRUE(WaitRx(2));
}

TEST_F(LinkTest, CrcErrorsAndUnknownMessagesAreCounted) {
  StartRunning();
  auto bad = BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  bad[bad.size() - 1] ^= 0xFF;
  Inject(bad);
  Inject(BuildFrame(2, 2, 5, 6, 777, std::vector<std::uint8_t>{1}, kExtra));  // unknown msgid
  Inject(BuildFrame(2, 3, 5, 6, 200, std::vector<std::uint8_t>{9, 9, 9, 9}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  const auto c = link_->counters();
  EXPECT_GE(c.rx_crc_err, 1U);
  EXPECT_GE(c.rx_dropped, 1U);
  EXPECT_EQ(c.rx_ok, 1U);
}

TEST_F(LinkTest, GarbageAndFalseStartsDoNotLoseFollowingFrames) {
  StartRunning();
  std::vector<std::uint8_t> bytes{0x00, 0xFD, 0xFD, 0x55, 0xFE, 0x10};
  const auto good = BuildFrame(2, 4, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  bytes.insert(bytes.end(), good.begin(), good.end());
  Inject(bytes);
  ASSERT_TRUE(WaitRx(1));
}

TEST_F(LinkTest, ParserIsResetWhenDriverRestartsSoPartialFrameDoesNotGlue) {
  cfg_.restart_delay = milliseconds(50);
  StartRunning();
  const auto frame = BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  Inject(std::vector<std::uint8_t>(frame.begin(), frame.begin() + 5));  // a truncated frame
  radio_->Fatal("restart");
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(2000)));
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(2000)));
  Inject(frame);  // a whole frame after the restart: it must arrive
  ASSERT_TRUE(WaitRx(1));
  EXPECT_EQ(RxCount(), 1U);
}

// ---------------------------------------------------------------- TX queues

TEST_F(LinkTest, RequestsAreSentInFifoOrder) {
  cfg_.fifo_limit = 100;
  StartRunning();
  radio_->SetHold(true);
  for (int i = 0; i < 10; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i + 1))));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(200, 10));
  const auto frames = Sent();
  for (int i = 0; i < 10; ++i) EXPECT_EQ(frames[i].payload[0], i + 1);
}

TEST_F(LinkTest, FifoLimitRejectsRequestImmediatelyAndDropsResponse) {
  cfg_.fifo_limit = 3;
  StartRunning();
  radio_->SetHold(true);  // the first frame hangs in Send(), the rest waits in the FIFO
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i + 2))));
  EXPECT_FALSE(link_->EnqueueRequest(Msg(200, 99)));  // FIFO full -> an error at once
  EXPECT_FALSE(link_->EnqueueResponse(Msg(201, 1)));  // RESP: dropped + counter
  EXPECT_EQ(link_->counters().tx_dropped, 1U);
}

TEST_F(LinkTest, EventsCoalesceToLatestValuePerKey) {
  StartRunning();
  radio_->SetHold(true);
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(0));  // will be held in Send()
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  for (int i = 2; i <= 9; ++i) link_->EnqueueEvent(7, Msg(202, static_cast<std::uint8_t>(i)), milliseconds(0));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(202, 1));
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(SentCount(201), 1U);
  EXPECT_EQ(SentCount(202), 1U);  // 8 updates -> one, the newest
  for (const auto& f : Sent()) {
    if (f.msgid == 202) {
      EXPECT_EQ(f.payload[0], 9);
    }
  }
}

TEST_F(LinkTest, RequestsGoBeforeEvents) {
  StartRunning();
  radio_->SetHold(true);
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(0));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  link_->EnqueueEvent(2, Msg(201, 2), milliseconds(0));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 3)));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(200, 1));
  ASSERT_TRUE(WaitSentCount(201, 2));
  const auto frames = Sent();
  ASSERT_EQ(frames.size(), 3U);
  EXPECT_EQ(frames[0].msgid, 201U);  // held before the REQ, not overtaken
  EXPECT_EQ(frames[1].msgid, 200U);  // the REQ goes before the remaining event
  EXPECT_EQ(frames[2].msgid, 201U);
}

TEST_F(LinkTest, EventRateLimitSendsAtMostConfiguredFrequency) {
  StartRunning();
  const auto t0 = Clock::now();
  for (int i = 0; i < 50; ++i) {
    link_->EnqueueEvent(1, Msg(201, static_cast<std::uint8_t>(i)), milliseconds(100));
    std::this_thread::sleep_for(milliseconds(10));
  }
  const auto ms = std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count();
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_LE(SentCount(201), static_cast<std::size_t>(ms / 100 + 2));
  EXPECT_GE(SentCount(201), 3U);
}

TEST_F(LinkTest, RateLimitedEventSendsLatestValueInNextWindowNotAQueue) {
  StartRunning();
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(200));
  ASSERT_TRUE(WaitSentCount(201, 1));
  for (int i = 2; i <= 6; ++i) link_->EnqueueEvent(1, Msg(201, static_cast<std::uint8_t>(i)), milliseconds(200));
  ASSERT_TRUE(WaitSentCount(201, 2, 1000));
  std::this_thread::sleep_for(milliseconds(300));
  const auto frames = Sent();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[1].payload[0], 6);
}

TEST_F(LinkTest, ZeroIntervalMeansNoLimit) {
  cfg_.fifo_limit = 100;
  StartRunning();
  for (int i = 0; i < 5; ++i) {
    link_->EnqueueEvent(static_cast<std::uint64_t>(i), Msg(201, static_cast<std::uint8_t>(i)), milliseconds(0));
  }
  ASSERT_TRUE(WaitSentCount(201, 5));
}

TEST_F(LinkTest, ExpiredRequestsAreDroppedInsteadOfSentLate) {
  StartRunning();
  radio_->SetHold(true);
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(0));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  OutFrame stale = Msg(200, 1);
  stale.deadline = Clock::now() + milliseconds(30);
  OutFrame fresh = Msg(200, 2);
  ASSERT_TRUE(link_->EnqueueRequest(stale));
  ASSERT_TRUE(link_->EnqueueRequest(fresh));
  std::this_thread::sleep_for(milliseconds(100));  // still expired in the queue
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(200, 1));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(SentCount(200), 1U);
  for (const auto& f : Sent()) {
    if (f.msgid == 200) {
      EXPECT_EQ(f.payload[0], 2);
    }
  }
  EXPECT_EQ(link_->counters().tx_expired, 1U);
}

TEST_F(LinkTest, DropEventsRemovesQueuedEventsOfAnInstance) {
  StartRunning();
  radio_->SetHold(true);
  link_->EnqueueEvent(0x10001, Msg(201, 1), milliseconds(0));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  link_->EnqueueEvent(0x20001, Msg(202, 2), milliseconds(0));  // instance 2
  link_->EnqueueEvent(0x30001, Msg(202, 3), milliseconds(0));  // instance 3
  link_->DropEvents(~std::uint64_t{0xFFFF}, 0x20000);          // unregistering instance 2
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(202, 1));
  std::this_thread::sleep_for(milliseconds(100));
  ASSERT_EQ(SentCount(202), 1U);
  for (const auto& f : Sent()) {
    if (f.msgid == 202) {
      EXPECT_EQ(f.payload[0], 3);
    }
  }
}

TEST_F(LinkTest, TxSequenceNumberIncrementsAndWraps) {
  cfg_.fifo_limit = 400;
  StartRunning();
  for (int i = 0; i < 300; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitSentCount(200, 300, 5000));
  const auto frames = Sent();
  for (std::size_t i = 0; i < frames.size(); ++i) EXPECT_EQ(frames[i].seq, static_cast<std::uint8_t>(i));
}

TEST_F(LinkTest, VersionOneLinkSendsV1Frames) {
  cfg_.link.version = 1;
  StartRunning();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitSentCount(200, 1));
  EXPECT_EQ(Sent().at(0).version, 1);
  EXPECT_EQ(radio_->sent().at(0).at(0), ara::com::mavlink::kMagicV1);
}

TEST_F(LinkTest, VersionTwoLinkSendsV2FramesWithTruncation) {
  StartRunning();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 5, 8)));  // 5,0,0,0,0,0,0,0
  ASSERT_TRUE(WaitSentCount(200, 1));
  const auto bytes = radio_->sent().at(0);
  EXPECT_EQ(bytes.at(0), ara::com::mavlink::kMagicV2);
  EXPECT_EQ(bytes.at(1), 1);  // payload length after zero truncation
}

// ---------------------------------------------------------------- kBusy / OnTxReady

TEST_F(LinkTest, BusyHoldsTheFrameUntilTxReadyWithoutLossOrDuplicate) {
  cfg_.fifo_limit = 100;
  StartRunning();
  radio_->SetHold(true);
  for (int i = 1; i <= 6; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i))));
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(radio_->sent_count(), 0U);       // nothing went through
  EXPECT_GE(link_->counters().tx_busy, 1U);  // and the driver reported kBusy
  radio_->SetHold(false);                    // OnTxReady
  ASSERT_TRUE(WaitSentCount(200, 6));
  std::this_thread::sleep_for(milliseconds(100));
  const auto frames = Sent();
  ASSERT_EQ(frames.size(), 6U);
  for (int i = 0; i < 6; ++i) EXPECT_EQ(frames[i].payload[0], i + 1);  // order and no duplicates
}

TEST_F(LinkTest, BusyHeartbeatDoesNotOvertakeTheHeldFrameKeepingSignatureOrder) {
  const auto key = TmpPath("radio_key_order");
  WriteKey(key, 3);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  cfg_.link.heartbeat_hz = 100;
  cfg_.fifo_limit = 100;
  StartRunning();
  radio_->SetHold(true);
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i))));
  std::this_thread::sleep_for(milliseconds(200));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(200, 5));
  std::uint64_t last = 0;
  for (const auto& f : Sent()) {
    EXPECT_GT(f.timestamp, last) << "signature timestamps must grow in transmit order";
    last = f.timestamp;
  }
  std::remove(key.c_str());
}

TEST_F(LinkTest, TxReadyArrivingBeforeTheLinkWaitsIsNotLost) {
  StartRunning();
  for (int round = 0; round < 20; ++round) {
    radio_->SetHold(true);
    ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(round))));
    while (radio_->busy_count() <= round) std::this_thread::sleep_for(milliseconds(1));
    radio_->SetHold(false);  // OnTxReady right after kBusy
    ASSERT_TRUE(WaitSentCount(200, static_cast<std::size_t>(round) + 1)) << "round " << round;
  }
  EXPECT_EQ(SentCount(200), 20U);
}

TEST_F(LinkTest, BusyForTooLongLogsWarningOnceAndKeepsTheFrame) {
  cfg_.busy_warn_after = milliseconds(100);
  StartRunning();
  radio_->SetHold(true);
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  std::this_thread::sleep_for(milliseconds(600));
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "radio busy"), 1) << logs_.Dump();
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(200, 1));
}

TEST_F(LinkTest, FatalWhileBusyDropsTheHeldFrameAndLinkRestarts) {
  cfg_.restart_delay = milliseconds(50);
  StartRunning();
  radio_->SetHold(true);
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  radio_->Fatal("dead");
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(2000)));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(2000)));
  std::this_thread::sleep_for(milliseconds(150));
  EXPECT_EQ(SentCount(200), 0U);  // the frame held during a restart is lost (the REQ would expire anyway)
  EXPECT_EQ(link_->counters().tx_dropped, 1U);
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 2)));  // a new REQ goes out after the restart
  ASSERT_TRUE(WaitSentCount(200, 1));
}

TEST_F(LinkTest, SendErrorOtherThanBusyRestartsTheLink) {
  cfg_.restart_delay = milliseconds(50);
  StartRunning();
  radio_->Stop();  // Send() will return kClosed: driver lost
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(2000)));
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(2000)));
  EXPECT_EQ(radio_->start_count(), 2);
}

// ---------------------------------------------------------------- signing

TEST_F(LinkTest, SignedTxIsVerifiableAndMonotonic) {
  const auto key = TmpPath("radio_key_a");
  WriteKey(key, 3);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  cfg_.link.signing_link_id = 4;
  cfg_.fifo_limit = 100;
  StartRunning();
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i))));
  ASSERT_TRUE(WaitSentCount(200, 5));
  LinkSigner::Config sc;
  for (int i = 0; i < 32; ++i) sc.key[i] = static_cast<std::uint8_t>(3 + i);
  sc.link_id = 1;
  LinkSigner verifier{sc, [this] { return now_; }};
  std::uint64_t last = 0;
  for (const auto& bytes : radio_->sent()) {
    FrameParser parser{Lookup};
    std::vector<Frame> parsed;
    parser.Feed(bytes, &parsed);
    ASSERT_EQ(parsed.size(), 1U);
    const Frame& f = parsed[0];
    EXPECT_TRUE(f.has_signature);
    EXPECT_EQ(f.link_id, 4);
    EXPECT_GT(f.timestamp, last);
    last = f.timestamp;
    EXPECT_EQ(verifier.VerifySignature(f, bytes), SignatureVerdict::kOk);
    EXPECT_EQ(verifier.CheckReplay(f), SignatureVerdict::kOk);
    verifier.Commit(f);
  }
  std::remove(key.c_str());
}

TEST_F(LinkTest, WrongKeyFileGivesInitErrorAndNothingIsSent) {
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = TmpPath("missing_key_xyz");
  cfg_.link.heartbeat_hz = 50;
  Make();
  link_->Start();
  EXPECT_EQ(link_->state(), RadioLinkState::kInitError);
  std::ignore = link_->EnqueueRequest(Msg(200, 1));  // the queue accepts it, but nothing goes out
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_EQ(radio_->sent_count(), 0U);
  EXPECT_EQ(radio_->start_attempts(), 0);  // the driver does not start at all
  EXPECT_EQ(logs_.Count(LogLevel::kError, "signing initialisation failed"), 1);
}

TEST_F(LinkTest, ShortKeyFileGivesInitError) {
  const auto key = TmpPath("radio_key_short");
  std::ofstream{key} << "abcd\n";
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  Make();
  link_->Start();
  EXPECT_EQ(link_->state(), RadioLinkState::kInitError);
  std::remove(key.c_str());
}

TEST_F(LinkTest, RxRejectsBadSignatureReplayAndUnsigned) {
  const auto key = TmpPath("radio_key_rx");
  WriteKey(key, 10);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  cfg_.link.accept_unsigned = false;
  StartRunning();
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
  Inject(BuildFrame(2, 1, 5, 6, 200, payload, kExtra, &ok1));       // OK
  Inject(BuildFrame(2, 2, 5, 6, 200, payload, kExtra, &wrongkey));  // wrong key
  Inject(BuildFrame(2, 3, 5, 6, 200, payload, kExtra, &replay));    // replay
  Inject(BuildFrame(2, 4, 5, 6, 200, payload, kExtra));             // unsigned
  Inject(BuildFrame(2, 5, 5, 6, 200, payload, kExtra, &ok2));       // OK
  ASSERT_TRUE(WaitRx(2));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(RxCount(), 2U);
  EXPECT_EQ(link_->counters().rx_ok, 2U);
  EXPECT_EQ(link_->counters().rx_sig_err, 3U);
  std::remove(key.c_str());
}

TEST_F(LinkTest, AcceptUnsignedLetsUnsignedFramesThrough) {
  const auto key = TmpPath("radio_key_lax");
  WriteKey(key, 10);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  cfg_.link.accept_unsigned = true;
  StartRunning();
  Inject(BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  EXPECT_EQ(link_->counters().rx_sig_err, 0U);
  std::remove(key.c_str());
}

TEST_F(LinkTest, SigningDisabledIgnoresSignatureOfIncomingFrames) {
  StartRunning();
  std::array<std::uint8_t, 32> k{};
  const SigningParams sp{k, 1, 5};
  Inject(BuildFrame(2, 1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra, &sp));
  ASSERT_TRUE(WaitRx(1));
}

TEST_F(LinkTest, SigningStatePersistedAndUsedAsFloorAfterRestart) {
  const auto key = TmpPath("radio_key_p");
  const auto state = TmpPath("radio_state_p");
  std::remove(state.c_str());
  WriteKey(key, 3);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  cfg_.link.state_file = state;
  cfg_.fifo_limit = 100;
  now_ = 0;  // BBB without an RTC
  StartRunning();
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitSentCount(200, 3));
  const std::uint64_t last_before = Sent().back().timestamp;
  link_->Stop();
  {
    std::ofstream out{state, std::ios::trunc};
    out << last_before << "\n";  // as PersistIfDue writes it
  }
  radio_ = std::make_shared<MemoryRadio>("radio2");
  StartRunning();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitSentCount(200, 1));
  EXPECT_GT(Sent().at(0).timestamp, last_before + ara::com::mavlink::kStartupFloor - 1);
  std::remove(key.c_str());
  std::remove(state.c_str());
}

TEST_F(LinkTest, ReplayWindowMovesOnlyForFramesTheHandlerAccepted) {
  const auto key = TmpPath("radio_key_window");
  WriteKey(key, 10);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  StartRunning();
  std::array<std::uint8_t, 32> good{};
  for (int i = 0; i < 32; ++i) good[i] = static_cast<std::uint8_t>(10 + i);
  const std::vector<std::uint8_t> payload{1, 2, 3};
  const SigningParams far_future{good, 5, 90000};
  const SigningParams later{good, 5, 2000};
  // The handler rejects the first frame (e.g. it is for another instance): its large timestamp
  // must not advance the replay window of the stream...
  accept_ = false;
  Inject(BuildFrame(2, 1, 5, 6, 200, payload, kExtra, &far_future));
  ASSERT_TRUE(WaitRx(1));
  // ...so a later frame with a smaller timestamp is still accepted as new.
  accept_ = true;
  Inject(BuildFrame(2, 2, 5, 6, 200, payload, kExtra, &later));
  ASSERT_TRUE(WaitRx(2));
  EXPECT_EQ(link_->counters().rx_sig_err, 0U);
  // Once accepted, the same timestamp is a replay.
  Inject(BuildFrame(2, 3, 5, 6, 200, payload, kExtra, &later));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(RxCount(), 2U);
  EXPECT_EQ(link_->counters().rx_sig_err, 1U);
  std::remove(key.c_str());
}

TEST_F(LinkTest, SigningTimestampsNeverFallBelowTheBuildDateEvenWithClockAt1970) {
  const auto key = TmpPath("radio_key_build_floor");
  WriteKey(key, 3);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;  // no state file
  now_ = 0;
  StartRunning();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitSentCount(200, 1));
  EXPECT_GE(Sent().back().timestamp, ara::com::mavlink::BuildTimestamp10us());
  std::remove(key.c_str());
}

}  // namespace
}  // namespace mavlink_daemon
}  // namespace srp
