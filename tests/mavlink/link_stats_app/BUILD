load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_runtime_lib", "ara_someip_lib")

# Test client of the MavlinkLinkService (platform_definition/mavlink_link_service) over the real
# `ipc`: `require` of the service offered by the MavlinkDaemon. It runs under Execution Management with
# someip_demon on a target or in QEMU (instructions: tests/mavlink/README.md, "LinkStats on real ipc").
# It prints `LINKSTATS ...` for every event and `GETLINKSTATS ...` for the method calls.

filegroup(
    name = "instance",
    srcs = [
        "link_stats_app.json",
        "@srp_platform//platform_definition/mavlink_link_service",
    ],
    visibility = ["//visibility:public"],
)

ara_runtime_lib(
    name = "ara",
    model_src = [":instance"],
    visibility = ["//visibility:private"],
)

ara_someip_lib(
    name = "someip_lib",
    model_src = [":instance"],
    visibility = ["//visibility:private"],
)

cc_binary(
    name = "link_stats_app_bin",
    srcs = [
        "link_stats_app.cc",
        "link_stats_app.h",
        "main.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        ":ara",
        ":someip_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//platform_definition/mavlink_link_service:link_name",
    ],
)

adaptive_application(
    name = "link_stats_app",
    bin = ":link_stats_app_bin",
    model_src = [":instance"],
    visibility = ["//visibility:public"],
)
