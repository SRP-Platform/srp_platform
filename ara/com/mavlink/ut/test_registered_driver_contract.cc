/**
 * @file test_registered_driver_contract.cc
 * @brief IRadioDriver contract tests on a driver created through the global registry (a
 * SerialRadio wrapper on a pty registered statically): the same suite as for `serial_radio`.
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/com/mavlink/testing/pty_peer.h"
#include "ara/com/mavlink/testing/radio_driver_contract_test.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::RadioDriverContractTest;

/// The config file lives as long as the driver.
struct ConfigKeeper {
  ~ConfigKeeper() {
    driver.reset();
    if (!config.empty()) {
      ::unlink(config.c_str());
    }
  }
  std::shared_ptr<IRadioDriver> driver;
  std::string config;
};

testing::RadioDriverHarness MakeRegisteredHarness() {
  auto pty = std::make_shared<testing::PtyPeer>();
  auto keeper = std::make_shared<ConfigKeeper>();
  static int counter = 0;
  keeper->config =
      "/tmp/srp_registered_radio_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".json";
  std::ofstream(keeper->config) << "{\"device\": \"" << pty->path() << "\", \"baudrate\": 115200}";
  testing::RadioDriverHarness h;
  const auto created = RadioDriverRegistry::Global().Create("test_serial_b", keeper->config);
  if (!created.HasValue()) {
    ADD_FAILURE() << created.Error().SupportData();
    return h;
  }
  keeper->driver = created.Value();
  h.driver = std::shared_ptr<IRadioDriver>(keeper, keeper->driver.get());  // the keeper lives with the driver
  h.peer_write = [pty](const std::vector<std::uint8_t>& v) { pty->Write(v); };
  h.peer_read = [pty](std::size_t want, std::uint32_t timeout_ms) { return pty->Read(want, timeout_ms); };
  h.overflow_bytes = 4 * 1024 * 1024;
  return h;
}

INSTANTIATE_TEST_SUITE_P(RegisteredDriver, RadioDriverContractTest,
                         ::testing::Values(testing::RadioContractParam{"registered_driver", MakeRegisteredHarness}),
                         testing::RadioContractParamName);

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
