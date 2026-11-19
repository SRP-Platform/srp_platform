load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["@srp_platform//platform/common/mavlink_daemon/model:instance"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "mavlink_daemon",
    srcs = [
        "main.cc",
        "mavlink_daemon.cc",
        "mavlink_daemon.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ara",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//platform/common/mavlink_daemon/code:daemon_config",
        "@srp_platform//platform/common/mavlink_daemon/code:mavlink_daemon_core",
    ],
)
