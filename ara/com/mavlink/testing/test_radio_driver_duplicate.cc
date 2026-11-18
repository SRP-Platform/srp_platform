/**
 * Copyright (c) 2024
 *
 * @file test_radio_driver_duplicate.cc
 * @brief TESTONLY: registers the id `test_serial_a` that `test_radio_driver` already registers
 * (the daemon must refuse to start and name the id).
 */
#include <memory>
#include <string>

#include "ara/com/mavlink/radio_driver_registry.h"
#include "ara/com/mavlink/serial_radio.h"

SRP_MAVLINK_REGISTER_DRIVER("test_serial_a", ara::com::mavlink::SerialRadioFactory());
