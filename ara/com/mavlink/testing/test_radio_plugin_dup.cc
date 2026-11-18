/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_dup.cc
 * @brief TESTONLY: plugin rejestrujący id zajęte przez test_radio_plugin (duplikat).
 */
#include <memory>
#include <string>

#include "ara/com/mavlink/radio_driver.h"
#include "ara/com/mavlink/serial_radio.h"

namespace {
void RegisterDrivers(ara::com::mavlink::IRadioDriverRegistry& registry) {  // NOLINT(runtime/references)
  registry.Register("test_serial_a", ara::com::mavlink::SerialRadioFactory());
}
}  // namespace

SRP_MAVLINK_DRIVER_PLUGIN(RegisterDrivers)
