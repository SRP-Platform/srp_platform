/**
 * @file radio_driver_contract_test.cc
 * @copyright Copyright (c) 2024
 */
#include "ara/com/mavlink/testing/radio_driver_contract_test.h"

#include <algorithm>
#include <chrono>  // NOLINT
#include <functional>
#include <stop_token>  // NOLINT
#include <tuple>
#include <utility>

#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {
namespace {
using Clock = std::chrono::steady_clock;

std::vector<std::uint8_t> Pattern(std::size_t n, std::uint8_t seed = 1) {
  std::vector<std::uint8_t> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<std::uint8_t>(seed + i * 7);
  }
  return v;
}

bool WaitFor(const std::function<bool()>& pred, std::uint32_t timeout_ms) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return pred();
}

std::vector<std::uint8_t> PeerReadAll(const RadioDriverHarness& h, std::size_t want, std::uint32_t total_ms) {
  std::vector<std::uint8_t> out;
  const auto deadline = Clock::now() + std::chrono::milliseconds(total_ms);
  while (out.size() < want && Clock::now() < deadline) {
    const auto part = h.peer_read(want - out.size(), 50);
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

std::int64_t ElapsedMs(Clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}
}  // namespace

// ------------------------------------------------------------------ RecordingListener

void RecordingListener::Note() {
  ++total_;
  if (sealed_.load()) {
    ++violations_;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  threads_.insert(std::this_thread::get_id());
}

void RecordingListener::OnReceive(std::span<const std::uint8_t> bytes) noexcept {
  Note();
  std::lock_guard<std::mutex> lock{mutex_};
  received_.insert(received_.end(), bytes.begin(), bytes.end());
}

void RecordingListener::OnTxReady() noexcept {
  Note();
  ++tx_ready_;
}

void RecordingListener::OnError(ara::core::ErrorCode, bool fatal) noexcept {
  Note();
  ++errors_;
  if (fatal) {
    ++fatal_errors_;
  }
}

std::vector<std::uint8_t> RecordingListener::received() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return received_;
}

std::size_t RecordingListener::received_size() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return received_.size();
}

std::set<std::thread::id> RecordingListener::threads() const {
  std::lock_guard<std::mutex> lock{mutex_};
  return threads_;
}

std::string RadioContractParamName(const ::testing::TestParamInfo<RadioContractParam>& info) {
  return info.param.name;
}

// ------------------------------------------------------------------------- testy

TEST_P(RadioDriverContractTest, NameIsStable) {
  EXPECT_FALSE(h_.driver->Name().empty());
  EXPECT_EQ(h_.driver->Name(), h_.driver->Name());
}

TEST_P(RadioDriverContractTest, StopBeforeStartAndTwiceIsHarmless) {
  h_.driver->Stop();
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  h_.driver->Stop();
  h_.driver->Stop();
}

TEST_P(RadioDriverContractTest, ReceivedBytesReachListener) {
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  const auto in = Pattern(100);
  h_.peer_write(in);
  ASSERT_TRUE(WaitFor([&] { return l.received_size() >= in.size(); }, 3000));
  EXPECT_EQ(l.received(), in);  // dowolne porcje, ale bez utraty i zmiany kolejności
}

TEST_P(RadioDriverContractTest, SendDeliversWholeFrame) {
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  const auto frame = Pattern(64, 9);
  const auto r = h_.driver->Send(frame);
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(PeerReadAll(h_, frame.size(), 3000), frame);
}

TEST_P(RadioDriverContractTest, CallbacksComeFromDriverThreadNotFromCaller) {
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  const auto frame = Pattern(40);
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(h_.driver->Send(frame).HasValue());
  }
  h_.peer_write(Pattern(50));
  ASSERT_TRUE(WaitFor([&] { return l.received_size() >= 50; }, 3000));
  const auto threads = l.threads();
  EXPECT_EQ(threads.count(std::this_thread::get_id()), 0U)
      << "callback listenera wywołany synchronnie w wątku wołającym Start()/Send()";
}

TEST_P(RadioDriverContractTest, SendNeverBlocksAndBusyIsFollowedByTxReady) {
  if (h_.overflow_bytes == 0) {
    GTEST_SKIP() << "harness bez przepełnienia bufora";
  }
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  constexpr std::size_t kFrame = 200;
  const std::size_t max_calls = h_.overflow_bytes / kFrame + 64;
  std::vector<std::uint8_t> accepted;
  bool busy = false;
  for (std::size_t i = 0; i < max_calls && !busy; ++i) {
    const auto frame = Pattern(kFrame, static_cast<std::uint8_t>(i));
    const auto t0 = Clock::now();
    const auto r = h_.driver->Send(frame);
    EXPECT_LT(ElapsedMs(t0), 250) << "Send() zablokował się (nr " << i << ")";
    if (r.HasValue()) {
      accepted.insert(accepted.end(), frame.begin(), frame.end());
    } else {
      ASSERT_TRUE(IsTransportError(r.Error(), TransportErrc::kBusy))
          << "oczekiwano kBusy, jest: " << r.Error().SupportData();
      busy = true;
    }
  }
  ASSERT_TRUE(busy) << "radio nie zgłosiło kBusy po " << h_.overflow_bytes << " B";

  // Peer zwalnia miejsce: driver musi zawołać OnTxReady(), a na przewodzie nie ma zapisu
  // częściowego ani duplikatu — dokładnie ramki przyjęte, w kolejności.
  const auto got = PeerReadAll(h_, accepted.size(), 10000);
  EXPECT_TRUE(WaitFor([&] { return l.tx_ready() >= 1; }, 3000)) << "brak OnTxReady() po kBusy";
  EXPECT_EQ(got.size(), accepted.size());
  EXPECT_EQ(got, accepted) << "bajty na przewodzie != ramki przyjęte przez Send() (zapis częściowy / utrata)";
  // po OnTxReady() Send znowu przyjmuje ramki
  const auto frame = Pattern(kFrame, 77);
  ASSERT_TRUE(h_.driver->Send(frame).HasValue());
  EXPECT_EQ(PeerReadAll(h_, frame.size(), 3000), frame);
}

TEST_P(RadioDriverContractTest, StopWhileWaitingForTxReadyAfterBusyReturnsPromptlyWithoutCallbacks) {
  if (h_.overflow_bytes == 0) {
    GTEST_SKIP() << "harness bez przepełnienia bufora";
  }
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  constexpr std::size_t kFrame = 200;
  const std::size_t max_calls = h_.overflow_bytes / kFrame + 64;
  bool busy = false;
  for (std::size_t i = 0; i < max_calls && !busy; ++i) {
    const auto r = h_.driver->Send(Pattern(kFrame, static_cast<std::uint8_t>(i)));
    if (!r.HasValue()) {
      ASSERT_TRUE(IsTransportError(r.Error(), TransportErrc::kBusy)) << r.Error().SupportData();
      busy = true;
    }
  }
  ASSERT_TRUE(busy) << "radio nie zgłosiło kBusy";
  // Daemon czeka na OnTxReady(); w tym momencie dostaje Stop() (np. zamykanie daemona albo restart łącza).
  const auto t0 = Clock::now();
  h_.driver->Stop();
  EXPECT_LT(ElapsedMs(t0), 2000) << "Stop() zawisł na oczekiwaniu na zwolnienie miejsca";
  l.Seal();  // od teraz każdy callback (także spóźnione OnTxReady) łamie kontrakt
  // Peer zwalnia miejsce PO Stop(): driver nie może już wołać OnTxReady().
  std::ignore = PeerReadAll(h_, h_.overflow_bytes, 500);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(l.violations(), 0) << "callback (np. OnTxReady) po powrocie ze Stop() w trakcie kBusy";
  const auto after = h_.driver->Send(Pattern(kFrame, 1));
  ASSERT_FALSE(after.HasValue());
  EXPECT_FALSE(IsTransportError(after.Error(), TransportErrc::kBusy)) << "po Stop() Send ma zwracać błąd, nie kBusy";
}

TEST_P(RadioDriverContractTest, ZeroCallbacksAfterStop) {
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  std::atomic<bool> feeding{true};
  std::thread feeder{[&] {
    while (feeding.load()) {
      h_.peer_write(Pattern(32));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }};
  ASSERT_TRUE(WaitFor([&] { return l.received_size() > 0; }, 3000));
  h_.driver->Stop();
  l.Seal();  // od teraz każdy callback łamie kontrakt
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  feeding = false;
  feeder.join();
  EXPECT_EQ(l.violations(), 0) << "callback po powrocie ze Stop()";
}

TEST_P(RadioDriverContractTest, StartAfterStopWorks) {
  {
    RecordingListener l1;
    ASSERT_TRUE(h_.driver->Start(l1, {}).HasValue());
    h_.peer_write(Pattern(10));
    ASSERT_TRUE(WaitFor([&] { return l1.received_size() >= 10; }, 3000));
    h_.driver->Stop();
  }
  RecordingListener l2;
  ASSERT_TRUE(h_.driver->Start(l2, {}).HasValue());
  const auto in = Pattern(20, 5);
  h_.peer_write(in);
  ASSERT_TRUE(WaitFor([&] { return l2.received_size() >= in.size(); }, 3000));
  EXPECT_EQ(l2.received(), in);
  const auto frame = Pattern(30, 3);
  ASSERT_TRUE(h_.driver->Send(frame).HasValue());
  EXPECT_EQ(PeerReadAll(h_, frame.size(), 3000), frame);
}

TEST_P(RadioDriverContractTest, SendAfterStopIsAnErrorNotBusy) {
  RecordingListener l;
  ASSERT_TRUE(h_.driver->Start(l, {}).HasValue());
  h_.driver->Stop();
  const auto r = h_.driver->Send(Pattern(10));
  ASSERT_FALSE(r.HasValue());
  EXPECT_FALSE(IsTransportError(r.Error(), TransportErrc::kBusy));
}

TEST_P(RadioDriverContractTest, StartWithCancelledTokenReturnsPromptly) {
  std::stop_source src;
  src.request_stop();
  RecordingListener l;
  const auto t0 = Clock::now();
  const auto r = h_.driver->Start(l, src.get_token());
  EXPECT_LT(ElapsedMs(t0), 2000);
  if (r.HasValue()) {
    h_.driver->Stop();
  }
}

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara
