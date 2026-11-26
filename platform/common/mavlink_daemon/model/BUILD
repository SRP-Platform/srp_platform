load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_someip_lib")

filegroup(
    name = "instance",
    srcs = [
        "mavlink_daemon_app.json",
        "@srp_platform//platform_definition/mavlink_link_service",
    ],
    visibility = ["//visibility:public"],
)

# The generated skeleton of the platform service MavlinkLinkService (offered on ipc).
ara_someip_lib(
    name = "someip_lib",
    model_src = [":instance"],
    visibility = ["@srp_platform//platform/common/mavlink_daemon:__subpackages__"],
)

# The daemon config (mavlink_daemon_config.json) goes to the application etc from the srp
# deployment: `etcs` below refers to the file from the deployment directory chosen by the
# integrator (example: docs/mavlink).
adaptive_application(
    name = "mavlink_daemon",
    bin = "@srp_platform//platform/common/mavlink_daemon/code/application:mavlink_daemon",
    model_src = ["@srp_platform//platform/common/mavlink_daemon/model:instance"],
    etcs = ["mavlink_daemon_config.json"],
    visibility = ["//visibility:public"],
)
