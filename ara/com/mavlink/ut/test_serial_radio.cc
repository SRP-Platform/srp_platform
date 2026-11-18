/**
 * @file test_serial_radio.cc
 * @brief serial_radio na pty: testy kontraktu IRadioDriver + konfiguracja i błędy startu.
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>  // NOLINT
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "ara/com/mavlink/serial_radio.h"
#include "ara/com/mavlink/testing/pty_peer.h"
#include "ara/com/mavlink/testing/radio_driver_contract_test.h"
#include "ara/com/mavlink/transport_error.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::RadioDriverContractTest;

testing::RadioDriverHarness MakeSerialRadioHarness() {
  auto pty = std::make_shared<testing::PtyPeer>();
  testing::RadioDriverHarness h;
  h.driver = std::make_shared<SerialRadio>("pty", pty->path(), 115200);
  h.peer_write = [pty](const std::vector<std::uint8_t>& v) { pty->Write(v); };
  h.peer_read = [pty](std::size_t want, std::uint32_t timeout_ms) { return pty->Read(want, timeout_ms); };
  h.overflow_bytes = 4 * 1024 * 1024;
  return h;
}

INSTANTIATE_TEST_SUITE_P(SerialRadio, RadioDriverContractTest,
                         ::testing::Values(testing::RadioContractParam{"pty", MakeSerialRadioHarness}),
                         testing::RadioContractParamName);

std::string WriteConfig(const std::string& body) {
  const std::string path = "/tmp/srp_serial_radio_cfg_" + std::to_string(::getpid()) + ".json";
  std::ofstream f(path);
  f << body;
  return path;
}

class RecordingNoop final : public IRadioListener {
 public:
  void OnReceive(std::span<const std::uint8_t>) noexcept override {}
  void OnTxReady() noexcept override {}
  void OnError(ara::core::ErrorCode, bool) noexcept override {}
};

TEST(SerialRadioConfig, ReadsDeviceAndBaudrateFromJson) {
  testing::PtyPeer pty;
  const auto cfg = WriteConfig("{\"device\": \"" + pty.path() + "\", \"baudrate\": 57600}");
  SerialRadio radio{cfg};
  EXPECT_NE(radio.Name().find(pty.path()), std::string::npos);
  RecordingNoop l;
  ASSERT_TRUE(radio.Start(l, {}).HasValue());
  radio.Stop();
  ::unlink(cfg.c_str());
}

TEST(SerialRadioConfig, FactoryBuildsDriverWithoutValidatingFileYet) {
  const auto driver = SerialRadioFactory()("/nonexistent/serial_radio.json");
  ASSERT_NE(driver, nullptr);
  RecordingNoop l;
  const auto r = driver->Start(l, {});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
  EXPECT_NE(r.Error().SupportData().find("/nonexistent/serial_radio.json"), std::string::npos);
}

TEST(SerialRadioConfig, RejectsIncompleteOrBadConfig) {
  RecordingNoop l;
  for (const char* body : {"{}", "{\"device\": \"/dev/null\"}", "{\"baudrate\": 9600}",
                           "{\"device\": \"\", \"baudrate\": 9600}", "{\"device\": \"/dev/null\", \"baudrate\": 12345}",
                           "{\"device\": \"/dev/null\", \"baudrate\": \"fast\"}", "not json"}) {
    const auto cfg = WriteConfig(body);
    SerialRadio radio{cfg};
    const auto r = radio.Start(l, {});
    EXPECT_FALSE(r.HasValue()) << body;
    if (!r.HasValue()) {
      EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent)) << body;
    }
    ::unlink(cfg.c_str());
  }
}

TEST(SerialRadioStart, MissingDeviceIsPersistentError) {
  SerialRadio radio{"x", "/dev/srp_no_such_tty", 9600};
  RecordingNoop l;
  const auto r = radio.Start(l, {});
  ASSERT_FALSE(r.HasValue());
  EXPECT_TRUE(IsTransportError(r.Error(), TransportErrc::kPersistent));
}

TEST(SerialRadioStart, StartTwiceWithoutStopIsRejected) {
  testing::PtyPeer pty;
  SerialRadio radio{"x", pty.path(), 9600};
  RecordingNoop l;
  ASSERT_TRUE(radio.Start(l, {}).HasValue());
  EXPECT_FALSE(radio.Start(l, {}).HasValue());
  radio.Stop();
}

TEST(SerialRadioStart, DeviceLossIsReportedAsFatalError) {
  auto pty = std::make_unique<testing::PtyPeer>();
  SerialRadio radio{"x", pty->path(), 115200};
  testing::RecordingListener l;
  ASSERT_TRUE(radio.Start(l, {}).HasValue());
  pty.reset();  // urządzenie zniknęło
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (l.fatal_errors() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_GE(l.fatal_errors(), 1);
  radio.Stop();  // po fatalu Stop() i tak działa
  EXPECT_FALSE(radio.Send(std::vector<std::uint8_t>{1, 2, 3}).HasValue());
}

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
