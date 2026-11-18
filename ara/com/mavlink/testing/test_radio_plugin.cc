/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin.cc
 * @brief TESTONLY: plugin `.so` rejestrujący 2 drivery (`test_serial_a`, `test_serial_b`) —
 * obudowy SerialRadio z własną nazwą. Służy testom loadera i kontraktu przez dlopen.
 */
#include <memory>
#include <string>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/serial_radio.h"

namespace {

/// Delegat zmieniający tylko Name() (dowód, który driver faktycznie powstał).
class NamedRadio final : public ara::com::mavlink::IRadioDriver {
 public:
  NamedRadio(std::string name, const std::string& config_path)
      : name_{std::move(name)}, inner_{config_path} {}
  ara::core::Result<void> Start(ara::com::mavlink::IRadioListener& l, std::stop_token t) noexcept override {
    return inner_.Start(l, t);
  }
  void Stop() noexcept override { inner_.Stop(); }
  ara::core::Result<void> Send(std::span<const std::uint8_t> f) noexcept override { return inner_.Send(f); }
  std::string Name() const noexcept override { return name_; }

 private:
  std::string name_;
  ara::com::mavlink::SerialRadio inner_;
};

void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {  // NOLINT(runtime/references)
  for (const char* id : {"test_serial_a", "test_serial_b"}) {
    const std::string name = id;
    registry.Register(name, [name](const std::string& cfg) {
      return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<NamedRadio>(name, cfg)};
    });
  }
}

}  // namespace

SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)
