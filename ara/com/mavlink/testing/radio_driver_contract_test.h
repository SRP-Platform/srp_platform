/**
 * Copyright (c) 2024
 *
 * @file radio_driver_contract_test.h
 * @brief TESTONLY: shared IRadioDriver contract test suite (radio_driver.h) — for
 * `serial_radio`, the test driver registered through the registry and the drivers in the srp repo.
 *
 * Usage (the suite name in the macro must be unqualified, hence the `using`):
 * @code
 *   using ara::com::mavlink::testing::RadioDriverContractTest;
 *   INSTANTIATE_TEST_SUITE_P(
 *       MyDriver, RadioDriverContractTest,
 *       ::testing::Values(ara::com::mavlink::testing::RadioContractParam{
 *           "my", [] { return MakeHarness(); }}),
 *       ara::com::mavlink::testing::RadioContractParamName);
 * @endcode
 *
 * The harness supplies the driver (not started yet) and the "other side of the air".
 * Checked: bytes from the peer reach OnReceive, Send delivers the whole frame, Send does not
 * block, kBusy + OnTxReady, no partial write, zero callbacks after Stop(), Start() after
 * Stop(), callbacks from a foreign thread, a Send() error after Stop(); for a driver with
 * `emit_link_stats`: OnLinkStats only from the driver thread and never after Stop().
 */
#ifndef ARA_COM_MAVLINK_TESTING_RADIO_DRIVER_CONTRACT_TEST_H_
#define ARA_COM_MAVLINK_TESTING_RADIO_DRIVER_CONTRACT_TEST_H_

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT
#include <set>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/radio_driver.h"

namespace ara {
namespace com {
namespace mavlink {
namespace testing {

struct RadioDriverHarness {
  /// The driver under test (not started yet).
  std::shared_ptr<IRadioDriver> driver;
  /// Bytes show up at the driver (OnReceive); called after Start().
  std::function<void(const std::vector<std::uint8_t>&)> peer_write;
  /// Bytes transmitted by the driver; returns up to `want` bytes (>= 1 if anything arrived within
  /// `timeout_ms`), an empty vector = nothing. Also called to "free room" after kBusy.
  std::function<std::vector<std::uint8_t>(std::size_t want, std::uint32_t timeout_ms)> peer_read;
  /// How many bytes certainly fill the radio when the peer reads nothing (0 = skip the kBusy tests).
  std::size_t overflow_bytes{0};
  /// Optional: makes the driver call `OnLinkStats(stats)` from its own thread (a driver that
  /// reports link statistics). Empty = the driver reports none; the OnLinkStats tests are skipped.
  std::function<void(const RadioLinkStats&)> emit_link_stats;
};

using RadioHarnessFactory = std::function<RadioDriverHarness()>;

struct RadioContractParam {
  std::string name;
  RadioHarnessFactory factory;
};

/// Test listener: records bytes, counts callbacks and threads, and checks that nothing arrives
/// after `Seal()`.
class RecordingListener final : public IRadioListener {
 public:
  void OnReceive(std::span<const std::uint8_t> bytes) noexcept override;
  void OnTxReady() noexcept override;
  void OnError(ara::core::ErrorCode e, bool fatal) noexcept override;
  void OnLinkStats(const RadioLinkStats& stats) noexcept override;

  int link_stats() const { return link_stats_.load(); }
  RadioLinkStats last_link_stats() const;
  std::vector<std::uint8_t> received() const;
  std::size_t received_size() const;
  int tx_ready() const { return tx_ready_.load(); }
  int errors() const { return errors_.load(); }
  int fatal_errors() const { return fatal_errors_.load(); }
  /// All callbacks (to detect traffic after Stop()).
  int total() const { return total_.load(); }
  std::set<std::thread::id> threads() const;
  /// From now on every callback is a contract violation (`violations()`).
  void Seal() { sealed_ = true; }
  int violations() const { return violations_.load(); }

 private:
  void Note();
  mutable std::mutex mutex_;
  std::vector<std::uint8_t> received_;
  std::set<std::thread::id> threads_;
  std::atomic<int> tx_ready_{0}, errors_{0}, fatal_errors_{0}, total_{0}, violations_{0}, link_stats_{0};
  RadioLinkStats last_link_stats_;
  std::atomic<bool> sealed_{false};
};

class RadioDriverContractTest : public ::testing::TestWithParam<RadioContractParam> {
 protected:
  void SetUp() override { h_ = GetParam().factory(); }
  void TearDown() override {
    if (h_.driver) {
      h_.driver->Stop();
    }
  }
  RadioDriverHarness h_;
};

std::string RadioContractParamName(const ::testing::TestParamInfo<RadioContractParam>& info);

}  // namespace testing
}  // namespace mavlink
}  // namespace com
}  // namespace ara

#endif  // ARA_COM_MAVLINK_TESTING_RADIO_DRIVER_CONTRACT_TEST_H_
