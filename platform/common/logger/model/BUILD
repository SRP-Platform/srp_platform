load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_runtime_lib", "ara_someip_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["//platform/common/logger/model:instance"],
    visibility = ["//visibility:public"],
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
    model_src = ["//platform/common/logger/model:instance"],
    visibility = ["//visibility:public"],
)
