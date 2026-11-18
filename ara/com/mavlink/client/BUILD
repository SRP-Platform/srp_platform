# Klient MavlinkDaemon po stronie aplikacji (bindingi `on: mavlink`), rev. 10.
cc_library(
    name = "daemon_client",
    srcs = [
        "daemon_client.cc",
        "mavlink_endpoint.cc",
    ],
    hdrs = [
        "daemon_client.h",
        "mavlink_endpoint.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com/mavlink:daemon_ipc",
        "@srp_platform//ara/com/mavlink:daemon_protocol",
        "@srp_platform//ara/com/mavlink:mavlink_codec",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

# DaemonClient::Instance() z logowaniem przez ara::log (osobna jednostka: testy klienta nie linkują ara::log).
cc_library(
    name = "daemon_client_global",
    srcs = ["daemon_client_instance.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":daemon_client",
        "@srp_platform//ara/com:com_log",
        "@srp_platform//ara/log",
    ],
)

# Atrapa daemona do testów klienta i bindingów (gniazdo + skryptowane odpowiedzi).
cc_library(
    name = "fake_daemon",
    testonly = True,
    srcs = ["testing/fake_daemon.cc"],
    hdrs = ["testing/fake_daemon.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com/mavlink:daemon_ipc",
        "@srp_platform//ara/com/mavlink:daemon_protocol",
    ],
)

cc_test(
    name = "test_daemon_client",
    srcs = ["ut/test_daemon_client.cc"],
    testonly = True,
    deps = [
        ":daemon_client",
        ":fake_daemon",
        "@com_google_googletest//:gtest_main",
    ],
)
