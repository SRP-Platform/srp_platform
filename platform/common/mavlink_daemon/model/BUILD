load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application")

filegroup(
    name = "instance",
    srcs = [
        "mavlink_daemon_app.json",
    ],
    visibility = ["//visibility:public"],
)

# Konfiguracja daemona (mavlink_daemon_config.json) trafia do etc aplikacji z deployment srp:
# `etcs` poniżej dotyczy pliku z katalogu deploymentu, który wybiera integrator (przykład: docs/mavlink).
adaptive_application(
    name = "mavlink_daemon",
    bin = "@srp_platform//platform/common/mavlink_daemon/code/application:mavlink_daemon",
    model_src = ["@srp_platform//platform/common/mavlink_daemon/model:instance"],
    etcs = ["mavlink_daemon_config.json"],
    visibility = ["//visibility:public"],
)
