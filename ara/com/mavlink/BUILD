load("@srp_platform//ara/com/mavlink:mavlink_driver_plugin.bzl", "mavlink_driver_plugin")

# Build date of the binary: the lower bound of MAVLink signing timestamps on a system without a
# set clock. The genrule result is cached by bazel, so the value is the date of the build that
# last re-ran it.
genrule(
    name = "build_time",
    outs = ["mavlink_build_time.h"],
    cmd = "printf '#pragma once\\nconstexpr unsigned long long kBuildUnixTime = %su;\\n' \"$$(date +%s)\" > $@",
)

# Frames, codec, signing, SHA-256, dedup, the transport error domain and the shared log type.
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
        "dedup_table.h",
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

# IPC between applications and the MavlinkDaemon: the message protocol and the SEQPACKET socket.
cc_library(
    name = "daemon_ipc",
    srcs = [
        "daemon_ipc.cc",
        "daemon_protocol.cc",
    ],
    hdrs = [
        "daemon_ipc.h",
        "daemon_protocol.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_core",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
    ],
)

# Radio driver API, the plugin loader (dlopen) and the built-in serial_radio driver.
cc_library(
    name = "radio_driver",
    srcs = [
        "radio_driver_loader.cc",
        "serial_radio.cc",
    ],
    hdrs = [
        "radio_driver.h",
        "radio_driver_loader.h",
        "serial_radio.h",
    ],
    linkopts = ["-ldl"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_core",
        "@srp_platform//ara/core:Result",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//core/serial:serial_port",
    ],
)

# ---------------------------------------------------------------- testonly

cc_library(
    name = "memory_radio",
    testonly = True,
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

# Shared IRadioDriver contract tests (also run by the plugins of srp).
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

mavlink_driver_plugin(
    name = "test_radio_plugin",
    testonly = True,
    srcs = ["testing/test_radio_plugin.cc"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_dup",
    testonly = True,
    srcs = ["testing/test_radio_plugin_variant.cc"],
    copts = ["-DPLUGIN_DUPLICATE"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_replace",
    testonly = True,
    srcs = ["testing/test_radio_plugin_variant.cc"],
    copts = ["-DPLUGIN_REPLACE"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_badversion",
    testonly = True,
    srcs = ["testing/test_radio_plugin_variant.cc"],
    copts = ["-DPLUGIN_BAD_VERSION"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_nosymbols",
    testonly = True,
    srcs = ["testing/test_radio_plugin_nosymbols.cc"],
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
    name = "test_dedup_table",
    srcs = ["ut/test_dedup_table.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_daemon_protocol",
    srcs = ["ut/test_daemon_protocol.cc"],
    testonly = True,
    deps = [
        ":daemon_ipc",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_daemon_ipc",
    srcs = ["ut/test_daemon_ipc.cc"],
    testonly = True,
    deps = [
        ":daemon_ipc",
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
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_radio_driver_loader",
    srcs = ["ut/test_radio_driver_loader.cc"],
    testonly = True,
    data = [
        ":libtest_radio_plugin.so",
        ":libtest_radio_plugin_badversion.so",
        ":libtest_radio_plugin_dup.so",
        ":libtest_radio_plugin_nosymbols.so",
        ":libtest_radio_plugin_replace.so",
    ],
    env = {
        "PLUGIN_OK": "$(rootpath :libtest_radio_plugin.so)",
        "PLUGIN_DUP": "$(rootpath :libtest_radio_plugin_dup.so)",
        "PLUGIN_REPLACE": "$(rootpath :libtest_radio_plugin_replace.so)",
        "PLUGIN_BADVER": "$(rootpath :libtest_radio_plugin_badversion.so)",
        "PLUGIN_NOSYM": "$(rootpath :libtest_radio_plugin_nosymbols.so)",
    },
    deps = [
        ":radio_driver",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_radio_plugin_contract",
    srcs = ["ut/test_radio_plugin_contract.cc"],
    testonly = True,
    data = [":libtest_radio_plugin.so"],
    env = {"PLUGIN_OK": "$(rootpath :libtest_radio_plugin.so)"},
    deps = [
        ":pty_peer",
        ":radio_driver",
        ":radio_driver_contract_test",
        "@com_google_googletest//:gtest_main",
    ],
)
