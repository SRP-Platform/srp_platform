load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application")

filegroup(
    name = "instance",
    srcs = [
        "someip_app.json",
    ],
    visibility = ["@srp_platform//platform/common/someip_demon/code/application:__subpackages__"]
)

adaptive_application(
    name = "someip_demon",
    bin = "@srp_platform//platform/common/someip_demon/code/application:someip_demon",
    model_src = ["@srp_platform//platform/common/someip_demon/model:instance"],
    etcs = ["endpoint_config.json"],
    visibility = ["//visibility:public"],
)
