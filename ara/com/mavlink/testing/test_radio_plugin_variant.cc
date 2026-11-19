/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_variant.cc
 * @brief TESTONLY: driver plugin variants for the loader tests, selected by one -D define:
 *  - PLUGIN_DUPLICATE: registers an id already taken by test_radio_plugin (a duplicate);
 *  - PLUGIN_REPLACE: registers `serial_radio`, replacing the built-in driver;
 *  - PLUGIN_BAD_VERSION: reports an incompatible driver API version.
 */
#include <cstdint>
#include <memory>
#include <string>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/serial_radio.h"

#if defined(PLUGIN_BAD_VERSION)

extern "C" __attribute__((visibility("default"))) std::uint32_t srp_mavlink_driver_api_version() {
  return ara::com::mavlink::kRadioDriverApiVersion + 99;
}

extern "C" __attribute__((visibility("default"))) void srp_mavlink_driver_register(
    ara::com::mavlink::IRadioDriverRegistry* registry) {
  registry->Register("never_registered", [](const std::string&) {
    return std::shared_ptr<ara::com::mavlink::IRadioDriver>{};
  });
}

#elif defined(PLUGIN_DUPLICATE)

namespace {
void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {  // NOLINT(runtime/references)
  registry.Register("test_serial_a", ara::com::mavlink::SerialRadioFactory());
}
}  // namespace

SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)

#elif defined(PLUGIN_REPLACE)

namespace {

class ReplacementRadio final : public ara::com::mavlink::IRadioDriver {
 public:
  explicit ReplacementRadio(const std::string& config_path) : inner_{config_path} {}
  ara::core::Result<void> Start(ara::com::mavlink::IRadioListener& l, std::stop_token t) noexcept override {
    return inner_.Start(l, t);
  }
  void Stop() noexcept override { inner_.Stop(); }
  ara::core::Result<void> Send(std::span<const std::uint8_t> f) noexcept override { return inner_.Send(f); }
  std::string Name() const noexcept override { return "replacement_serial_radio"; }

 private:
  ara::com::mavlink::SerialRadio inner_;
};

void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {  // NOLINT(runtime/references)
  registry.Register("serial_radio", [](const std::string& cfg) {
    return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<ReplacementRadio>(cfg)};
  });
}

}  // namespace

SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)

#else
#error "select a plugin variant with -DPLUGIN_DUPLICATE, -DPLUGIN_REPLACE or -DPLUGIN_BAD_VERSION"
#endif
