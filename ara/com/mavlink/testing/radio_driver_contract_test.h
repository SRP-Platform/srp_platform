/**
 * Copyright (c) 2024
 *
 * @file radio_driver_contract_test.h
 * @brief TESTONLY: wspólny zestaw testów kontraktu IRadioDriver (radio_driver.h) — dla
 * `serial_radio`, testowego pluginu `.so` i pluginów w repo srp.
 *
 * Użycie (nazwa zestawu w makrze musi być niekwalifikowana, stąd `using`):
 * @code
 *   using ara::com::mavlink::testing::RadioDriverContractTest;
 *   INSTANTIATE_TEST_SUITE_P(
 *       MyDriver, RadioDriverContractTest,
 *       ::testing::Values(ara::com::mavlink::testing::RadioContractParam{
 *           "my", [] { return MakeHarness(); }}),
 *       ara::com::mavlink::testing::RadioContractParamName);
 * @endcode
 *
 * Harness dostarcza driver (jeszcze nie wystartowany) oraz „drugą stronę eteru”.
 * Sprawdzane: bajty z peera trafiają do OnReceive, Send dostarcza ramkę w całości,
 * Send nie blokuje, kBusy + OnTxReady, brak zapisu częściowego, zero callbacków po
 * Stop(), Start() po Stop(), callbacki z obcego wątku, błąd Send() po Stop().
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
  /// Testowany driver (jeszcze nie wystartowany).
  std::shared_ptr<IRadioDriver> driver;
  /// Bajty pojawiają się u drivera (OnReceive); wołane po Start().
  std::function<void(const std::vector<std::uint8_t>&)> peer_write;
  /// Bajty nadane przez driver; zwraca do `want` bajtów (>= 1, jeśli cokolwiek dotarło w
  /// `timeout_ms`), pusty wektor = nic. Wołane też do „zwolnienia miejsca” po kBusy.
  std::function<std::vector<std::uint8_t>(std::size_t want, std::uint32_t timeout_ms)> peer_read;
  /// Ile bajtów na pewno zapełni radio, gdy peer nic nie czyta (0 = pomiń testy kBusy).
  std::size_t overflow_bytes{0};
};

using RadioHarnessFactory = std::function<RadioDriverHarness()>;

struct RadioContractParam {
  std::string name;
  RadioHarnessFactory factory;
};

/// Listener testowy: zapamiętuje bajty, liczy callbacki, wątki i sprawdza, że po `Seal()`
/// nic już nie przychodzi.
class RecordingListener final : public IRadioListener {
 public:
  void OnReceive(std::span<const std::uint8_t> bytes) noexcept override;
  void OnTxReady() noexcept override;
  void OnError(ara::core::ErrorCode e, bool fatal) noexcept override;

  std::vector<std::uint8_t> received() const;
  std::size_t received_size() const;
  int tx_ready() const { return tx_ready_.load(); }
  int errors() const { return errors_.load(); }
  int fatal_errors() const { return fatal_errors_.load(); }
  /// Wszystkie callbacki (do wykrycia ruchu po Stop()).
  int total() const { return total_.load(); }
  std::set<std::thread::id> threads() const;
  /// Od teraz każdy callback to naruszenie kontraktu (`violations()`).
  void Seal() { sealed_ = true; }
  int violations() const { return violations_.load(); }

 private:
  void Note();
  mutable std::mutex mutex_;
  std::vector<std::uint8_t> received_;
  std::set<std::thread::id> threads_;
  std::atomic<int> tx_ready_{0}, errors_{0}, fatal_errors_{0}, total_{0}, violations_{0};
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
