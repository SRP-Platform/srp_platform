/**
 * Copyright (c) 2024
 *
 * @file test_radio_driver.cc
 * @brief TESTONLY: registers 2 drivers (`test_serial_a`, `test_serial_b`) statically, the way a
 * driver in srp does it — SerialRadio wrappers with their own name.
 */
#include <memory>
#include <string>
#include <utility>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/com/mavlink/serial_radio.h"

namespace {

/// A delegate that only changes Name() (proof of which driver was actually created).
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

ara::com::mavlink::RadioDriverFactory Named(const std::string& name) {
  return [name](const std::string& cfg) {
    return std::shared_ptr<ara::com::mavlink::IRadioDriver>{std::make_shared<NamedRadio>(name, cfg)};
  };
}

}  // namespace

SRP_MAVLINK_REGISTER_DRIVER("test_serial_a", Named("test_serial_a"));
SRP_MAVLINK_REGISTER_DRIVER("test_serial_b", Named("test_serial_b"));
