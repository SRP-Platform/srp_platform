load("@srp_platform//ara/com/mavlink:mavlink_driver_plugin.bzl", "mavlink_driver_plugin")

cc_library(
    name = "transport_error",
    srcs = ["transport_error.cc"],
    hdrs = ["transport_error.h"],
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//ara/core:error_lib"],
)

# Interfejs transportu bajtów: stabilny kontrakt platforma <-> aplikacja/srp.
cc_library(
    name = "mavlink_log",
    hdrs = ["mavlink_log.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "sha256",
    srcs = ["sha256.cc"],
    hdrs = ["sha256.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "dedup_table",
    hdrs = ["dedup_table.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mavlink_frame",
    srcs = ["mavlink_frame.cc"],
    hdrs = ["mavlink_frame.h"],
    visibility = ["//visibility:public"],
    deps = [":sha256"],
)

cc_library(
    name = "mavlink_codec",
    srcs = ["mavlink_codec.cc"],
    hdrs = ["mavlink_codec.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_frame",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

cc_library(
    name = "mavlink_signing",
    srcs = ["mavlink_signing.cc"],
    hdrs = ["mavlink_signing.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_frame",
        ":transport_error",
        "@srp_platform//ara/core:Result",
    ],
)

# Protokół IPC aplikacja <-> MavlinkDaemon (rev. 10) + gniazdo SOCK_SEQPACKET.
cc_library(
    name = "daemon_protocol",
    srcs = ["daemon_protocol.cc"],
    hdrs = ["daemon_protocol.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "daemon_ipc",
    srcs = ["daemon_ipc.cc"],
    hdrs = ["daemon_ipc.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":daemon_protocol",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
    ],
)

# API drivera radia (rev. 10) + rejestr/loader pluginów .so + wbudowany serial_radio.
cc_library(
    name = "radio_driver",
    hdrs = ["radio_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":transport_error",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "radio_driver_loader",
    srcs = ["radio_driver_loader.cc"],
    hdrs = ["radio_driver_loader.h"],
    linkopts = ["-ldl"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_log",
        ":radio_driver",
        ":transport_error",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "serial_radio",
    srcs = ["serial_radio.cc"],
    hdrs = ["serial_radio.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":radio_driver",
        ":transport_error",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//core/serial:serial_port",
    ],
)

# ---------------------------------------------------------------- testonly

cc_test(
    name = "test_transport_error",
    srcs = ["ut/test_transport_error.cc"],
    testonly = True,
    deps = [
        ":transport_error",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_frame",
    srcs = ["ut/test_mavlink_frame.cc"],
    testonly = True,
    deps = [
        ":mavlink_frame",
        ":sha256",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_codec",
    srcs = ["ut/test_mavlink_codec.cc"],
    testonly = True,
    deps = [
        ":mavlink_codec",
        ":mavlink_frame",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_signing",
    srcs = ["ut/test_mavlink_signing.cc"],
    testonly = True,
    deps = [
        ":mavlink_signing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_dedup_table",
    srcs = ["ut/test_dedup_table.cc"],
    testonly = True,
    deps = [
        ":dedup_table",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_daemon_protocol",
    srcs = ["ut/test_daemon_protocol.cc"],
    testonly = True,
    deps = [
        ":daemon_protocol",
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

# ---------------------------------------------------------------- testy API drivera

cc_library(
    name = "memory_radio",
    testonly = True,
    hdrs = ["testing/memory_radio.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":radio_driver",
        ":transport_error",
    ],
)

cc_library(
    name = "pty_peer",
    testonly = True,
    hdrs = ["testing/pty_peer.h"],
    visibility = ["//visibility:public"],
)

# Wspólny zestaw testów kontraktu IRadioDriver (do odpalenia także w pluginach srp).
cc_library(
    name = "radio_driver_contract_test",
    testonly = True,
    srcs = ["testing/radio_driver_contract_test.cc"],
    hdrs = ["testing/radio_driver_contract_test.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":radio_driver",
        ":transport_error",
        "@com_google_googletest//:gtest",
    ],
    alwayslink = True,
)

mavlink_driver_plugin(
    name = "test_radio_plugin",
    testonly = True,
    srcs = ["testing/test_radio_plugin.cc"],
    deps = [":serial_radio"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_ctl",
    testonly = True,
    srcs = ["testing/test_radio_plugin_ctl.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":serial_radio",
        "@srp_platform//core/json:simba_json",
    ],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_dup",
    testonly = True,
    srcs = ["testing/test_radio_plugin_dup.cc"],
    deps = [":serial_radio"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_replace",
    testonly = True,
    srcs = ["testing/test_radio_plugin_replace.cc"],
    deps = [":serial_radio"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_badversion",
    testonly = True,
    srcs = ["testing/test_radio_plugin_badversion.cc"],
)

mavlink_driver_plugin(
    name = "test_radio_plugin_nosymbols",
    testonly = True,
    srcs = ["testing/test_radio_plugin_nosymbols.cc"],
)

cc_test(
    name = "test_serial_radio",
    srcs = ["ut/test_serial_radio.cc"],
    testonly = True,
    deps = [
        ":pty_peer",
        ":radio_driver_contract_test",
        ":serial_radio",
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
        ":radio_driver_loader",
        ":serial_radio",
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
        ":radio_driver_contract_test",
        ":radio_driver_loader",
        "@com_google_googletest//:gtest_main",
    ],
)
