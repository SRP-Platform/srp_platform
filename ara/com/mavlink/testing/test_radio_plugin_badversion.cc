/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_badversion.cc
 * @brief TESTONLY: plugin zgłaszający niezgodną wersję API drivera.
 */
#include <cstdint>

#include "ara/com/mavlink/radio_driver.h"

extern "C" __attribute__((visibility("default"))) std::uint32_t srp_mavlink_driver_api_version() {
  return ara::com::mavlink::kRadioDriverApiVersion + 99;
}

extern "C" __attribute__((visibility("default"))) void srp_mavlink_driver_register(
    ara::com::mavlink::IRadioDriverRegistry* registry) {
  registry->Register("never_registered", [](const std::string&) {
    return std::shared_ptr<ara::com::mavlink::IRadioDriver>{};
  });
}
