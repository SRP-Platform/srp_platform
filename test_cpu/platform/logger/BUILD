load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "platform_runtime_lib", "platform_someip_lib")

platform_runtime_lib(
    name = "platform",
    model_src = ["//deployment/platform/logger:instance"],
    visibility = ["//visibility:public"],
)

platform_someip_lib(
    name = "someip_lib",
    model_src = ["//deployment/platform/logger:instance"],
)

filegroup(
    name = "instance",
    srcs = [
        "logger_app.json",
    ],
    visibility = ["//example_adaptive/ExampleApp/code:__subpackages__"],
)

adaptive_application(
    name = "logger",
    bin = "//platform/common/logger/code:logger",
    etcs = ["config.json"],
    model_src = ["//deployment/platform/logger:instance"],
    # tar_path = "platform/",
    visibility = ["//visibility:public"],
)
