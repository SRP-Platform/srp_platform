load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")

package(default_visibility = ["//visibility:public"])

# Example radio driver: one MAVLink frame = one UDP datagram. alwayslink: the driver registers
# itself from a static initializer, so the linker must keep the object file.
cc_library(
    name = "example_udp_radio",
    srcs = ["udp_radio.cc"],
    hdrs = ["udp_radio.h"],
    deps = [
        "@srp_platform//ara/com/mavlink:mavlink_core",
        "@srp_platform//ara/com/mavlink:radio_driver",
        "@srp_platform//ara/core:Result",
        "@srp_platform//core/json:simba_json",
    ],
    alwayslink = True,
)

# A daemon binary with the driver linked in (the platform application under Execution Management);
# the only thing an integrator writes. Config: example_udp_daemon_config.json.
mavlink_daemon_binary(
    name = "example_udp_daemon",
    drivers = [":example_udp_radio"],
)

exports_files(["example_udp_daemon_config.json"])

cc_test(
    name = "udp_radio_contract_test",
    srcs = ["udp_radio_contract_test.cc"],
    testonly = True,
    deps = [
        ":example_udp_radio",
        "@srp_platform//ara/com/mavlink:radio_driver",
        "@srp_platform//ara/com/mavlink:radio_driver_contract_test",
        "@com_google_googletest//:gtest_main",
    ],
)
exports_files(["udp_ground.json"])
