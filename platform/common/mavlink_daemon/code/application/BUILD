load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["@srp_platform//platform/common/mavlink_daemon/model:instance"],
    visibility = ["//visibility:public"],
)

# The platform service MavlinkLinkService (event LinkStats + method GetLinkStats) of the daemon, offered on ipc.
cc_library(
    name = "link_service_lib",
    hdrs = ["link_service.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com/mavlink:daemon_ipc",
        "@srp_platform//platform/common/mavlink_daemon/model:someip_lib",
        "@srp_platform//platform_definition/mavlink_link_service:link_name",
    ],
)

cc_test(
    name = "test_link_service",
    srcs = ["test_link_service.cc"],
    deps = [
        ":link_service_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

# The daemon application (ara::exec life cycle); the binary is built by `mavlink_daemon_binary`
# (defs.bzl), which adds the main() and the radio drivers.
cc_library(
    name = "daemon_lib",
    srcs = ["mavlink_daemon.cc"],
    hdrs = ["mavlink_daemon.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ara",
        ":link_service_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//platform/common/mavlink_daemon/code:daemon_config",
        "@srp_platform//platform/common/mavlink_daemon/code:mavlink_daemon_core",
    ],
)

# The same core without Execution Management, controlled through JSON lines on stdin/stdout
# (`mavlink_daemon_binary(host = True)`): for tests on a PC.
cc_library(
    name = "daemon_host_lib",
    srcs = ["daemon_host.cc"],
    hdrs = ["daemon_host.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_json//:json",
        "@srp_platform//platform/common/mavlink_daemon/code:daemon_config",
        "@srp_platform//platform/common/mavlink_daemon/code:mavlink_daemon_core",
    ],
)

# The platform daemon: serial_radio only.
mavlink_daemon_binary(
    name = "mavlink_daemon",
    visibility = ["//visibility:public"],
)
