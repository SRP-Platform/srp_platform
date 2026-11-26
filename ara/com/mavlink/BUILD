# Build date of the binary: the lower bound of MAVLink signing timestamps on a system without a
# set clock. The genrule result is cached by bazel, so the value is the date of the build that
# last re-ran it.
genrule(
    name = "build_time",
    outs = ["mavlink_build_time.h"],
    cmd = "printf '#pragma once\\nconstexpr unsigned long long kBuildUnixTime = %su;\\n' \"$$(date +%s)\" > $@",
)

# Frames, codec, signing, SHA-256, the transport error domain and the shared log type.
cc_library(
    name = "mavlink_core",
    srcs = [
        "mavlink_codec.cc",
        "mavlink_frame.cc",
        "mavlink_signing.cc",
        "sha256.cc",
        "transport_error.cc",
    ],
    hdrs = [
        "mavlink_codec.h",
        "mavlink_frame.h",
        "mavlink_log.h",
        "mavlink_signing.h",
        "sha256.h",
        "transport_error.h",
        ":build_time",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core:error_lib",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

# Radio driver API and the driver registry (static registration, SRP_MAVLINK_REGISTER_DRIVER).
cc_library(
    name = "radio_driver",
    srcs = ["radio_driver_registry.cc"],
    hdrs = [
        "radio_driver.h",
        "radio_driver_registry.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_core",
        "@srp_platform//ara/core:Result",
    ],
)

# The `serial_radio` driver. alwayslink: it registers itself from a static initializer.
cc_library(
    name = "serial_radio",
    srcs = ["serial_radio.cc"],
    hdrs = ["serial_radio.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_core",
        ":radio_driver",
        "@srp_platform//ara/core:Result",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//core/serial:serial_port",
    ],
    alwayslink = True,
)

# ---------------------------------------------------------------- testonly

# Test driver; not for production (it is linked only into test binaries and the test package
# //tests/mavlink/link_stats_cpu:pkg).
cc_library(
    name = "memory_radio",
    hdrs = ["testing/memory_radio.h"],
    visibility = ["//visibility:public"],
    deps = [":radio_driver"],
)

cc_library(
    name = "pty_peer",
    testonly = True,
    hdrs = ["testing/pty_peer.h"],
    visibility = ["//visibility:public"],
)

# Shared IRadioDriver contract tests (also run by the drivers in srp).
cc_library(
    name = "radio_driver_contract_test",
    testonly = True,
    srcs = ["testing/radio_driver_contract_test.cc"],
    hdrs = ["testing/radio_driver_contract_test.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":radio_driver",
        "@com_google_googletest//:gtest",
    ],
    alwayslink = True,
)

# TESTONLY drivers registered statically (like a driver in srp): `test_serial_a` and
# `test_serial_b` (SerialRadio wrappers with their own name).
cc_library(
    name = "test_radio_driver",
    testonly = True,
    srcs = ["testing/test_radio_driver.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":radio_driver",
        ":serial_radio",
    ],
    alwayslink = True,
)

# TESTONLY drivers on MemoryRadio for the end-to-end test of MavlinkLinkService on the real ipc
# (tests/mavlink/README.md): `test_stats_a` reports nothing, `test_stats_b` pushes OnLinkStats.
cc_library(
    name = "test_stats_radio_driver",
    srcs = ["testing/test_stats_radio_driver.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":memory_radio",
        ":radio_driver",
    ],
    alwayslink = True,
)

# TESTONLY: registers `test_serial_a` once more (a duplicate id when linked with
# :test_radio_driver; used by the daemon start-error test).
cc_library(
    name = "test_radio_driver_duplicate",
    testonly = True,
    srcs = ["testing/test_radio_driver_duplicate.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":radio_driver",
        ":serial_radio",
    ],
    alwayslink = True,
)

# ---------------------------------------------------------------- tests

cc_test(
    name = "test_mavlink_frame",
    srcs = ["ut/test_mavlink_frame.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_codec",
    srcs = ["ut/test_mavlink_codec.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_signing",
    srcs = ["ut/test_mavlink_signing.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_serial_radio",
    srcs = ["ut/test_serial_radio.cc"],
    testonly = True,
    deps = [
        ":pty_peer",
        ":radio_driver",
        ":radio_driver_contract_test",
        ":serial_radio",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_radio_driver_registry",
    srcs = ["ut/test_radio_driver_registry.cc"],
    testonly = True,
    deps = [
        ":radio_driver",
        ":serial_radio",
        ":test_radio_driver",
        "@com_google_googletest//:gtest_main",
    ],
)

# The contract suite on a driver created through the global registry (as the daemon does it).
cc_test(
    name = "test_registered_driver_contract",
    srcs = ["ut/test_registered_driver_contract.cc"],
    testonly = True,
    deps = [
        ":pty_peer",
        ":radio_driver",
        ":radio_driver_contract_test",
        ":test_radio_driver",
        "@com_google_googletest//:gtest_main",
    ],
)

# The contract suite on MemoryRadio (OnLinkStats rules included).
cc_test(
    name = "test_memory_radio_contract",
    srcs = ["ut/test_memory_radio_contract.cc"],
    testonly = True,
    deps = [
        ":memory_radio",
        ":radio_driver",
        ":radio_driver_contract_test",
        "@com_google_googletest//:gtest_main",
    ],
)
