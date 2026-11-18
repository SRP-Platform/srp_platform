/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_replace.cc
 * @brief TESTONLY: plugin o id `serial_radio` — zastępuje wbudowany driver.
 */
#include <memory>
#include <string>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/serial_radio.h"

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
