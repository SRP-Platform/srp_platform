/**
 * @file test_radio_plugin_contract.cc
 * @brief IRadioDriver contract tests on a driver from a `.so` plugin loaded through dlopen
 * (a SerialRadio wrapper on a pty): the same suite as for the built-in `serial_radio`.
 * @copyright Copyright (c) 2024
 */
#include <gtest/gtest.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ara/com/mavlink/radio_driver_loader.h"
#include "ara/com/mavlink/testing/pty_peer.h"
#include "ara/com/mavlink/testing/radio_driver_contract_test.h"

namespace ara {
namespace com {
namespace mavlink {
namespace {
using testing::RadioDriverContractTest;

/// The registry + loader + config file live as long as the driver.
struct PluginKeeper {
  PluginKeeper() : loader{&registry} {}
  ~PluginKeeper() {
    driver.reset();
    if (!config.empty()) {
      ::unlink(config.c_str());
    }
  }
  RadioDriverRegistry registry;
  RadioDriverLoader loader;
  std::shared_ptr<IRadioDriver> driver;
  std::string config;
};

testing::RadioDriverHarness MakePluginHarness() {
  auto pty = std::make_shared<testing::PtyPeer>();
  auto keeper = std::make_shared<PluginKeeper>();
  char buf[PATH_MAX];
  const char* env = std::getenv("PLUGIN_OK");
  const std::string so = (env != nullptr && ::realpath(env, buf) != nullptr) ? std::string{buf} : std::string{};
  const auto loaded = keeper->loader.LoadPlugin(so);
  testing::RadioDriverHarness h;
  if (!loaded.HasValue()) {
    ADD_FAILURE() << "LoadPlugin(" << so << "): " << loaded.Error().SupportData();
    return h;
  }
  static int counter = 0;
  keeper->config = "/tmp/srp_plugin_radio_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".json";
  std::ofstream(keeper->config) << "{\"device\": \"" << pty->path() << "\", \"baudrate\": 115200}";
  const auto created = keeper->registry.Create("test_serial_b", keeper->config);
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

INSTANTIATE_TEST_SUITE_P(DlopenPlugin, RadioDriverContractTest,
                         ::testing::Values(testing::RadioContractParam{"plugin_so", MakePluginHarness}),
                         testing::RadioContractParamName);

}  // namespace
}  // namespace mavlink
}  // namespace com
}  // namespace ara
