/**
 * Copyright (c) 2024
 *
 * @file test_radio_link.cc
 * @brief RadioLink on a MemoryRadio: states and driver restart, queues (FIFO, coalescing,
 * rate limit), kBusy/OnTxReady without loss or duplicate, v2 signing (TX/RX/replay/persist).
 */
#include <gtest/gtest.h>

#include <sys/stat.h>

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
#include <span>
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
    cfg_.restart_backoff = {milliseconds(60), milliseconds(60)};
  }
  void TearDown() override {
    if (link_) link_->Stop();
  }

  void Make() {
    link_ = std::make_unique<RadioLink>(
        cfg_, driver_override_ ? driver_override_ : radio_, Lookup,
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
  std::shared_ptr<ara::com::mavlink::IRadioDriver> driver_override_;  // replaces radio_ when set
  std::unique_ptr<RadioLink> link_;
  Logs logs_;
  std::mutex rx_mu_;
  std::vector<Frame> rx_;
  std::vector<bool> ready_events_;
  std::atomic<bool> accept_{true};  // the result the frame handler reports to the link
  std::uint64_t now_{ara::com::mavlink::kClockSanityThreshold + 1000};
};

// ---------------------------------------------------------------- states / driver restart

TEST_F(LinkTest, DriverLinkStatsAreKeptWithTheirTimeAndTxFramesAreCounted) {
  StartRunning();
  EXPECT_FALSE(link_->link_stats().have);
  ara::com::mavlink::RadioLinkStats st;
  st.valid = ara::com::mavlink::RadioLinkStats::kRssi | ara::com::mavlink::RadioLinkStats::kSnr;
  st.rssi_dbm = -71;
  st.snr_db = 12;
  radio_->LinkStats(st);
  for (int i = 0; i < 100 && !link_->link_stats().have; ++i) std::this_thread::sleep_for(milliseconds(5));
  const auto snap = link_->link_stats();
  ASSERT_TRUE(snap.have);
  EXPECT_EQ(snap.stats.valid, st.valid);
  EXPECT_EQ(snap.stats.rssi_dbm, -71);
  EXPECT_EQ(snap.stats.snr_db, 12);
  EXPECT_EQ(link_->counters().tx_frames, 0U);
  ASSERT_TRUE(link_->EnqueueResponse(Msg(200, 1)));
  ASSERT_TRUE(radio_->WaitSent(1, milliseconds(2000)));
  EXPECT_EQ(link_->counters().tx_frames, 1U);
}

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
  cfg_.restart_backoff = {milliseconds(500), milliseconds(500)};
  radio_->FailStarts(1000, true);
  Make();
  link_->Start();
  EXPECT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(1000)));
  EXPECT_FALSE(link_->IsReady());
}

TEST_F(LinkTest, StopWhileWaitingForRestartReturnsPromptly) {
  cfg_.restart_backoff = {milliseconds(60000), milliseconds(60000)};
  radio_->FailStarts(1000, true);
  Make();
  link_->Start();
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(1000)));
  const auto t0 = Clock::now();
  link_->Stop();
  EXPECT_LT(std::chrono::duration_cast<milliseconds>(Clock::now() - t0).count(), 2000);
}

TEST_F(LinkTest, FatalErrorStopsDriverAndRestartsItAfterTheDelay) {
  cfg_.restart_backoff = {milliseconds(200), milliseconds(200)};
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

// ---------------------------------------------------------------- no built-in periodic traffic

TEST_F(LinkTest, LinkWithoutApplicationTrafficSendsNothingFor5Seconds) {
  StartRunning();
  std::this_thread::sleep_for(milliseconds(5000));
  EXPECT_EQ(radio_->sent_count(), 0U);
}

TEST_F(LinkTest, ConfiguredSystemAndComponentIdsAreUsedOnTransmit) {
  cfg_.system_id = 11;
  cfg_.component_id = 3;
  StartRunning();
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitSentCount(200, 1));
  const auto f = Sent().at(0);
  EXPECT_EQ(f.sysid, 11);
  EXPECT_EQ(f.compid, 3);
}

TEST_F(LinkTest, ReceivedFrameOfMsgId0IsUnknownAndNotDelivered) {
  StartRunning();
  Inject(BuildFrame(1, 5, 6, 0, std::vector<std::uint8_t>{0, 0, 0, 0, 1, 2, 3, 4, 3}, 50));
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_EQ(RxCount(), 0U);
  EXPECT_EQ(link_->counters().rx_ok, 0U);
}

// ---------------------------------------------------------------- RX

TEST_F(LinkTest, ReceivedFrameReachesHandlerEvenWhenSplitIntoSmallChunks) {
  StartRunning();
  const auto bytes = BuildFrame(9, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  radio_->Inject(bytes, 3);  // 3 B chunks from the driver thread
  ASSERT_TRUE(WaitRx(1));
  std::lock_guard<std::mutex> lock{rx_mu_};
  EXPECT_EQ(rx_[0].msgid, 200U);
  EXPECT_EQ(rx_[0].sysid, 5);
  EXPECT_EQ(rx_[0].payload, (std::vector<std::uint8_t>{1, 2, 3, 4}));
}

// A MAVLink v1 frame with a correct CRC (the library no longer builds v1).
static std::vector<std::uint8_t> V1Frame(std::uint8_t seq, std::uint8_t msgid, std::uint8_t crc_extra) {
  std::vector<std::uint8_t> f{ara::com::mavlink::kMagicV1, 2, seq, 5, 6, msgid, 1, 2};
  std::uint16_t crc = ara::com::mavlink::CrcCalculate(std::span<const std::uint8_t>{f.data() + 1, f.size() - 1});
  crc = ara::com::mavlink::CrcAccumulate(crc, crc_extra);
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>(crc >> 8));
  return f;
}

TEST_F(LinkTest, V1FrameIsDroppedAndCountedWhileV2IsAccepted) {
  StartRunning();
  Inject(V1Frame(1, 200, kExtra));
  Inject(BuildFrame(2, 5, 6, 201, std::vector<std::uint8_t>{3, 4}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  const auto c = link_->counters();
  EXPECT_EQ(c.rx_dropped, 1U);  // the v1 frame
  EXPECT_EQ(c.rx_ok, 1U);
  EXPECT_EQ(RxCount(), 1U);  // only the v2 frame reached the handler
  std::lock_guard<std::mutex> lock{rx_mu_};
  EXPECT_EQ(rx_[0].msgid, 201U);
}

TEST_F(LinkTest, CrcErrorsAndUnknownMessagesAreCounted) {
  StartRunning();
  auto bad = BuildFrame(1, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  bad[bad.size() - 1] ^= 0xFF;
  Inject(bad);
  Inject(BuildFrame(2, 5, 6, 777, std::vector<std::uint8_t>{1}, kExtra));  // unknown msgid
  Inject(BuildFrame(3, 5, 6, 200, std::vector<std::uint8_t>{9, 9, 9, 9}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  const auto c = link_->counters();
  EXPECT_GE(c.rx_crc_err, 1U);
  EXPECT_GE(c.rx_dropped, 1U);
  EXPECT_EQ(c.rx_ok, 1U);
}

TEST_F(LinkTest, GarbageAndFalseStartsDoNotLoseFollowingFrames) {
  StartRunning();
  std::vector<std::uint8_t> bytes{0x00, 0xFD, 0xFD, 0x55, 0xFE, 0x10};
  const auto good = BuildFrame(4, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
  bytes.insert(bytes.end(), good.begin(), good.end());
  Inject(bytes);
  ASSERT_TRUE(WaitRx(1));
}

TEST_F(LinkTest, ParserIsResetWhenDriverRestartsSoPartialFrameDoesNotGlue) {
  cfg_.restart_backoff = {milliseconds(50), milliseconds(50)};
  StartRunning();
  const auto frame = BuildFrame(1, 5, 6, 200, std::vector<std::uint8_t>{1, 2, 3, 4}, kExtra);
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

TEST_F(LinkTest, RequestWithoutADeadlineExpiresAfterTheMethodTimeoutOfTheLink) {
  cfg_.link.method_timeout_ms = 1000;  // the default
  StartRunning();
  radio_->SetHold(true);
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(0));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  std::this_thread::sleep_for(milliseconds(800));
  EXPECT_EQ(link_->counters().tx_expired, 0U);  // not yet
  std::this_thread::sleep_for(milliseconds(500));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(201, 1));
  std::this_thread::sleep_for(milliseconds(100));
  EXPECT_EQ(SentCount(200), 0U);
  EXPECT_EQ(link_->counters().tx_expired, 1U);
}

TEST_F(LinkTest, ALongerMethodTimeoutOfTheLinkKeepsTheRequestInTheQueue) {
  cfg_.link.method_timeout_ms = 4000;
  StartRunning();
  radio_->SetHold(true);
  link_->EnqueueEvent(1, Msg(201, 1), milliseconds(0));
  while (radio_->busy_count() == 0) std::this_thread::sleep_for(milliseconds(2));
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  std::this_thread::sleep_for(milliseconds(1500));
  radio_->SetHold(false);
  ASSERT_TRUE(WaitSentCount(200, 1));
  EXPECT_EQ(link_->counters().tx_expired, 0U);
}

// ---------------------------------------------------------------- TX pacing (air_rate_bps)

// Msg(id, first, 28): 12 + 28 = 40 B on the air (the budget counts the full payload; the frame
// sent is shorter because v2 truncates the trailing zeros).
TEST_F(LinkTest, TxPacingSendsThirtyFramesOf40BytesAt9600BpsInAboutOneSecond) {
  cfg_.link.air_rate_bps = 9600;  // 1200 B/s
  cfg_.link.max_utilization = 1.0;
  StartRunning();
  const auto t0 = Clock::now();
  for (int i = 0; i < 30; ++i) {
    link_->EnqueueEvent(static_cast<std::uint64_t>(i), Msg(201, static_cast<std::uint8_t>(i), 28), milliseconds(0));
  }
  ASSERT_TRUE(WaitSentCount(201, 30, 3000));
  const double s = std::chrono::duration<double>(Clock::now() - t0).count();
  // one-frame bucket: the first frame goes at once, 29 x 33.3 ms for the rest = 0.967 s
  EXPECT_GE(s, 0.85) << s;
  EXPECT_LE(s, 1.15) << s;
}

TEST_F(LinkTest, TxPacingUsesTheMaxUtilizationShare) {
  cfg_.link.air_rate_bps = 9600;
  cfg_.link.max_utilization = 0.5;  // 600 B/s: 15 frames of 40 B take ~ 14 x 66.7 ms
  StartRunning();
  const auto t0 = Clock::now();
  for (int i = 0; i < 15; ++i) {
    link_->EnqueueEvent(static_cast<std::uint64_t>(i), Msg(201, static_cast<std::uint8_t>(i), 28), milliseconds(0));
  }
  ASSERT_TRUE(WaitSentCount(201, 15, 3000));
  const double s = std::chrono::duration<double>(Clock::now() - t0).count();
  EXPECT_GE(s, 0.93 * 0.85) << s;
  EXPECT_LE(s, 0.93 * 1.15) << s;
}

TEST_F(LinkTest, WithoutAirRateThereIsNoPacing) {
  cfg_.link.max_utilization = 0.1;  // ignored without air_rate_bps
  cfg_.link.mtu = 58;
  StartRunning();
  const auto t0 = Clock::now();
  for (int i = 0; i < 30; ++i) {
    link_->EnqueueEvent(static_cast<std::uint64_t>(i), Msg(201, static_cast<std::uint8_t>(i), 28), milliseconds(0));
  }
  ASSERT_TRUE(WaitSentCount(201, 30, 3000));
  EXPECT_LT(std::chrono::duration<double>(Clock::now() - t0).count(), 0.3);
}

TEST_F(LinkTest, TxPacingCountsTheSignatureOfASignedLink) {
  const std::string key = TmpPath("pace_key.hex");
  WriteKey(key, 3);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  cfg_.link.air_rate_bps = 9600;
  cfg_.link.max_utilization = 1.0;  // 12 + 28 + 13 = 53 B: 30 frames = 29 x 44.2 ms = 1.28 s
  StartRunning();
  const auto t0 = Clock::now();
  for (int i = 0; i < 30; ++i) {
    link_->EnqueueEvent(static_cast<std::uint64_t>(i), Msg(201, static_cast<std::uint8_t>(i), 28), milliseconds(0));
  }
  ASSERT_TRUE(WaitSentCount(201, 30, 4000));
  const double s = std::chrono::duration<double>(Clock::now() - t0).count();
  EXPECT_GE(s, 1.28 * 0.85) << s;
  EXPECT_LE(s, 1.28 * 1.15) << s;
  std::remove(key.c_str());
}

TEST_F(LinkTest, TxPacingKeepsOnlyTheNewestValueOfAnEventAndSendsAboutTheBudgetRate) {
  cfg_.link.air_rate_bps = 1600;  // 200 B/s = 5 frames of 40 B per second
  cfg_.link.max_utilization = 1.0;
  StartRunning();
  for (int i = 0; i < 60; ++i) {  // an event updated 20 times per second for 3 s
    link_->EnqueueEvent(1, Msg(201, static_cast<std::uint8_t>(i), 28), milliseconds(0));
    std::this_thread::sleep_for(milliseconds(50));
  }
  std::this_thread::sleep_for(milliseconds(500));
  std::vector<std::uint8_t> values;
  for (const auto& f : Sent()) {
    if (f.msgid == 201) values.push_back(f.payload[0]);
  }
  EXPECT_GE(values.size(), 13U);  // 5 / s over ~3 s (first one at once)
  EXPECT_LE(values.size(), 18U);
  ASSERT_FALSE(values.empty());
  EXPECT_EQ(values.back(), 59);  // the newest value, never a stale one
  for (std::size_t i = 1; i < values.size(); ++i) {
    EXPECT_GT(values[i], values[i - 1]);  // never a queue of old values: a frame carries the latest
    EXPECT_LE(values[i] - values[i - 1], 8);
  }
}

TEST_F(LinkTest, TxPacingStillExpiresRequestsWaitingForTheBudget) {
  cfg_.link.air_rate_bps = 800;  // 100 B/s: a 40 B frame every 400 ms
  cfg_.link.max_utilization = 1.0;
  StartRunning();
  OutFrame first = Msg(200, 1, 28);
  OutFrame late1 = Msg(200, 2, 28);
  OutFrame late2 = Msg(200, 3, 28);
  late1.deadline = late2.deadline = Clock::now() + milliseconds(250);
  ASSERT_TRUE(link_->EnqueueRequest(first));
  ASSERT_TRUE(link_->EnqueueRequest(late1));
  ASSERT_TRUE(link_->EnqueueRequest(late2));
  std::this_thread::sleep_for(milliseconds(700));
  EXPECT_EQ(SentCount(200), 1U);
  EXPECT_EQ(link_->counters().tx_expired, 2U);
}

TEST_F(LinkTest, TxPacingDoesNotDelayAFrameAfterAnIdleTime) {
  cfg_.link.air_rate_bps = 1600;
  cfg_.link.max_utilization = 1.0;
  StartRunning();
  link_->EnqueueEvent(1, Msg(201, 1, 28), milliseconds(0));
  ASSERT_TRUE(WaitSentCount(201, 1));
  std::this_thread::sleep_for(milliseconds(600));  // idle: more than a frame time (200 ms)
  const auto t0 = Clock::now();
  link_->EnqueueEvent(1, Msg(201, 2, 28), milliseconds(0));
  ASSERT_TRUE(WaitSentCount(201, 2));
  EXPECT_LT(std::chrono::duration<double>(Clock::now() - t0).count(), 0.1);
  link_->EnqueueEvent(1, Msg(201, 3, 28), milliseconds(0));  // right after: the bucket is one frame
  const auto t1 = Clock::now();
  ASSERT_TRUE(WaitSentCount(201, 3));
  EXPECT_GE(std::chrono::duration<double>(Clock::now() - t1).count(), 0.12);
}

TEST_F(LinkTest, TxPacingNeverSendsTwoFramesBackToBackAfterAShortWait) {
  cfg_.link.air_rate_bps = 1600;  // 40 B = 200 ms on the air
  cfg_.link.max_utilization = 1.0;
  StartRunning();
  link_->EnqueueEvent(1, Msg(201, 1, 28), milliseconds(0));
  ASSERT_TRUE(WaitSentCount(201, 1));
  // 300 ms later: more than one frame time (200 ms) but less than two. The bucket holds one frame, so the
  // second of two frames queued now must still wait a whole frame time after the first.
  std::this_thread::sleep_for(milliseconds(300));
  link_->EnqueueEvent(2, Msg(201, 2, 28), milliseconds(0));
  link_->EnqueueEvent(3, Msg(201, 3, 28), milliseconds(0));
  ASSERT_TRUE(WaitSentCount(201, 2));
  const auto t1 = Clock::now();
  ASSERT_TRUE(WaitSentCount(201, 3));
  EXPECT_GE(std::chrono::duration<double>(Clock::now() - t1).count(), 0.17);
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

TEST_F(LinkTest, LinkAlwaysSendsV2FramesWithTruncation) {
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

TEST_F(LinkTest, BusyHeldFrameKeepsSignatureOrder) {
  const auto key = TmpPath("radio_key_order");
  WriteKey(key, 3);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
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
  cfg_.restart_backoff = {milliseconds(50), milliseconds(50)};
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
  cfg_.restart_backoff = {milliseconds(50), milliseconds(50)};
  StartRunning();
  radio_->Stop();  // Send() will return kClosed: driver lost
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 1)));
  ASSERT_TRUE(WaitState(RadioLinkState::kRestartWait, milliseconds(2000)));
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(2000)));
  EXPECT_EQ(radio_->start_count(), 2);
}

/// A driver whose Send() always rejects frames with kRetryable (e.g. ICMP port unreachable).
class RejectingRadio : public ara::com::mavlink::IRadioDriver {
 public:
  ara::core::Result<void> Start(ara::com::mavlink::IRadioListener&, std::stop_token) noexcept override {  // NOLINT
    return {};
  }
  void Stop() noexcept override {}
  ara::core::Result<void> Send(std::span<const std::uint8_t>) noexcept override {
    return ara::com::mavlink::MakeTransportError(ara::com::mavlink::TransportErrc::kRetryable, "port unreachable");
  }
  std::string Name() const noexcept override { return "rejecting"; }
};

TEST_F(LinkTest, RejectedFramesAreLoggedOnceAndThenAtMostOnceEvery10SecondsWithTheCount) {
  struct Fake {
    std::mutex mu;
    Clock::time_point now{Clock::now()};
  };
  auto clock = std::make_shared<Fake>();
  cfg_.steady_now = [clock] {
    std::lock_guard<std::mutex> lock{clock->mu};
    return clock->now;
  };
  driver_override_ = std::make_shared<RejectingRadio>();
  StartRunning();
  const auto wait_dropped = [&](std::uint64_t n) {
    const auto end = Clock::now() + milliseconds(2000);
    while (link_->counters().tx_dropped < n && Clock::now() < end) std::this_thread::sleep_for(milliseconds(2));
    return link_->counters().tx_dropped >= n;
  };
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(link_->EnqueueRequest(Msg(200, static_cast<std::uint8_t>(i))));
  ASSERT_TRUE(wait_dropped(5));
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "Send() rejected"), 1) << logs_.Dump();
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "frame dropped"), 1) << logs_.Dump();
  {
    std::lock_guard<std::mutex> lock{clock->mu};
    clock->now += std::chrono::seconds(10);
  }
  ASSERT_TRUE(link_->EnqueueRequest(Msg(200, 9)));
  ASSERT_TRUE(wait_dropped(6));
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "Send() rejected"), 2) << logs_.Dump();
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "5 frames dropped in the last 10 s: port unreachable"), 1) << logs_.Dump();
  EXPECT_EQ(link_->counters().tx_dropped, 6U);
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
  Make();
  link_->Start();
  EXPECT_EQ(link_->state(), RadioLinkState::kInitError);
  std::ignore = link_->EnqueueRequest(Msg(200, 1));  // the queue accepts it, but nothing goes out
  std::this_thread::sleep_for(milliseconds(300));
  EXPECT_EQ(radio_->sent_count(), 0U);
  EXPECT_EQ(radio_->start_attempts(), 0);  // the driver does not start at all
  EXPECT_EQ(logs_.Count(LogLevel::kError, "signing initialisation failed"), 1);
}

TEST_F(LinkTest, KeyFileWiderThan0600GivesExactlyOneWarning) {
  const auto key = TmpPath("radio_key_perm_wide");
  WriteKey(key, 3);
  ASSERT_EQ(::chmod(key.c_str(), 0644), 0);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  StartRunning();
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "wider than 0600"), 1) << logs_.Dump();
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "0644"), 1) << logs_.Dump();
  std::remove(key.c_str());
}

TEST_F(LinkTest, KeyFile0600GivesNoPermissionWarning) {
  const auto key = TmpPath("radio_key_perm_ok");
  WriteKey(key, 3);
  ASSERT_EQ(::chmod(key.c_str(), 0600), 0);
  cfg_.link.signing_enabled = true;
  cfg_.link.key_file = key;
  StartRunning();
  EXPECT_EQ(logs_.Count(LogLevel::kWarn, "wider than 0600"), 0) << logs_.Dump();
  std::remove(key.c_str());
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
  Inject(BuildFrame(1, 5, 6, 200, payload, kExtra, &ok1));       // OK
  Inject(BuildFrame(2, 5, 6, 200, payload, kExtra, &wrongkey));  // wrong key
  Inject(BuildFrame(3, 5, 6, 200, payload, kExtra, &replay));    // replay
  Inject(BuildFrame(4, 5, 6, 200, payload, kExtra));             // unsigned
  Inject(BuildFrame(5, 5, 6, 200, payload, kExtra, &ok2));       // OK
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
  Inject(BuildFrame(1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra));
  ASSERT_TRUE(WaitRx(1));
  EXPECT_EQ(link_->counters().rx_sig_err, 0U);
  std::remove(key.c_str());
}

TEST_F(LinkTest, SigningDisabledIgnoresSignatureOfIncomingFrames) {
  StartRunning();
  std::array<std::uint8_t, 32> k{};
  const SigningParams sp{k, 1, 5};
  Inject(BuildFrame(1, 5, 6, 200, std::vector<std::uint8_t>{1}, kExtra, &sp));
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
  Inject(BuildFrame(1, 5, 6, 200, payload, kExtra, &far_future));
  ASSERT_TRUE(WaitRx(1));
  // ...so a later frame with a smaller timestamp is still accepted as new.
  accept_ = true;
  Inject(BuildFrame(2, 5, 6, 200, payload, kExtra, &later));
  ASSERT_TRUE(WaitRx(2));
  EXPECT_EQ(link_->counters().rx_sig_err, 0U);
  // Once accepted, the same timestamp is a replay.
  Inject(BuildFrame(3, 5, 6, 200, payload, kExtra, &later));
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

// ---------------------------------------------------------------- restart backoff

/// Test clock + wait: the supervisor "sleeps" by advancing a fake steady clock and records every
/// requested pause. Shared state: the link thread may outlive the test body's locals.
struct FakeRestartClock {
  std::mutex mu;
  Clock::time_point now{};
  std::vector<std::int64_t> waits_ms;
  void Advance(milliseconds d) {
    std::lock_guard<std::mutex> lock{mu};
    now += d;
  }
  std::size_t WaitCount() {
    std::lock_guard<std::mutex> lock{mu};
    return waits_ms.size();
  }
  std::vector<std::int64_t> Waits() {
    std::lock_guard<std::mutex> lock{mu};
    return waits_ms;
  }
};

TEST_F(LinkTest, RestartBackoffDoublesToTheMaximumAndResetsAfterATenSecondStart) {
  auto clock = std::make_shared<FakeRestartClock>();
  cfg_.restart_backoff = {milliseconds(500), milliseconds(30000)};
  cfg_.steady_now = [clock] {
    std::lock_guard<std::mutex> lock{clock->mu};
    return clock->now;
  };
  cfg_.wait = [clock](milliseconds d, std::stop_token st) {
    std::lock_guard<std::mutex> lock{clock->mu};
    clock->waits_ms.push_back(d.count());
    clock->now += d;
    return !st.stop_requested();
  };
  const auto expect_near = [](std::int64_t actual, std::int64_t want) {
    EXPECT_NEAR(static_cast<double>(actual), static_cast<double>(want), want * 0.05) << "want " << want;
  };
  const auto wait_for_waits = [&](std::size_t n) {
    const auto end = Clock::now() + milliseconds(3000);
    while (clock->WaitCount() < n && Clock::now() < end) std::this_thread::sleep_for(milliseconds(2));
    return clock->WaitCount() >= n;
  };

  // 8 failed Start() calls: 500, 1000, ... doubling up to the 30000 ms maximum, then it stays there.
  radio_->FailStarts(8, false);
  Make();
  link_->Start();
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  const std::vector<std::int64_t> want{500, 1000, 2000, 4000, 8000, 16000, 30000, 30000};
  auto waits = clock->Waits();
  ASSERT_EQ(waits.size(), want.size());
  for (std::size_t i = 0; i < want.size(); ++i) expect_near(waits[i], want[i]);

  // A fatal error after a short run continues the series (no reset)...
  clock->Advance(milliseconds(5000));
  radio_->Fatal("short run");
  ASSERT_TRUE(wait_for_waits(9));
  expect_near(clock->Waits()[8], 30000);
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  // ...9.9 s is still not enough...
  clock->Advance(milliseconds(9900));
  radio_->Fatal("9.9 s run");
  ASSERT_TRUE(wait_for_waits(10));
  expect_near(clock->Waits()[9], 30000);
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  // ...but a start that lasted 10 s resets the pause to the minimum, and the series begins again.
  clock->Advance(milliseconds(10000));
  radio_->Fatal("10 s run");
  ASSERT_TRUE(wait_for_waits(11));
  expect_near(clock->Waits()[10], 500);
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  clock->Advance(milliseconds(1000));
  radio_->Fatal("1 s run");
  ASSERT_TRUE(wait_for_waits(12));
  expect_near(clock->Waits()[11], 1000);
  link_->Stop();
}

TEST_F(LinkTest, RestartBackoffWithEqualMinAndMaxIsAConstantPause) {
  auto clock = std::make_shared<FakeRestartClock>();
  cfg_.restart_backoff = {milliseconds(700), milliseconds(700)};
  cfg_.wait = [clock](milliseconds d, std::stop_token st) {
    std::lock_guard<std::mutex> lock{clock->mu};
    clock->waits_ms.push_back(d.count());
    return !st.stop_requested();
  };
  radio_->FailStarts(4, false);
  Make();
  link_->Start();
  ASSERT_TRUE(WaitState(RadioLinkState::kRunning, milliseconds(3000)));
  EXPECT_EQ(clock->Waits(), (std::vector<std::int64_t>{700, 700, 700, 700}));
  link_->Stop();
}

}  // namespace
}  // namespace mavlink_daemon
}  // namespace srp
