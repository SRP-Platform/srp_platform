cc_library(
    name = "daemon_config",
    srcs = ["config/daemon_config.cc"],
    hdrs = ["config/daemon_config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_json//:json",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "radio_link",
    srcs = ["link/radio_link.cc"],
    hdrs = ["link/radio_link.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":daemon_config",
        "@srp_platform//ara/com/mavlink:mavlink_core",
        "@srp_platform//ara/com/mavlink:radio_driver",
        "@srp_platform//ara/core:Result",
        "@srp_platform//core/common:condition_lib",
    ],
)

cc_library(
    name = "mavlink_daemon_core",
    srcs = ["core/mavlink_daemon_core.cc"],
    hdrs = ["core/mavlink_daemon_core.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":daemon_config",
        ":radio_link",
        "@srp_platform//ara/com/mavlink:daemon_ipc",
        "@srp_platform//ara/com/mavlink:mavlink_core",
        "@srp_platform//ara/com/mavlink:radio_driver",
        "@srp_platform//ara/core:Result",
        "@srp_platform//core/common:condition_lib",
    ],
)

# ---------------------------------------------------------------- tests

cc_library(
    name = "daemon_test_support",
    testonly = True,
    hdrs = ["ut/daemon_test_support.h"],
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//ara/com/mavlink:daemon_ipc"],
)

cc_test(
    name = "test_daemon_config",
    srcs = ["ut/test_daemon_config.cc"],
    testonly = True,
    deps = [
        ":daemon_config",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_radio_link",
    srcs = ["ut/test_radio_link.cc"],
    testonly = True,
    deps = [
        ":radio_link",
        "@com_google_googletest//:gtest_main",
        "@srp_platform//ara/com/mavlink:memory_radio",
    ],
)

cc_test(
    name = "test_mavlink_daemon_core",
    srcs = ["ut/test_mavlink_daemon_core.cc"],
    testonly = True,
    deps = [
        ":daemon_test_support",
        ":mavlink_daemon_core",
        "@com_google_googletest//:gtest_main",
        "@srp_platform//ara/com/mavlink:memory_radio",
    ],
)
