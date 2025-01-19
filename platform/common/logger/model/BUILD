load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_runtime_lib", "ara_someip_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["@srp_platform//platform/common/logger/model:instance"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "instance",
    srcs = [
        "logger_app.json",
    ],
    visibility = ["@srp_platform//example_adaptive/ExampleApp/code:__subpackages__"],
)

adaptive_application(
    name = "logger",
    bin = "@srp_platform//platform/common/logger/code:logger",
    model_src = ["@srp_platform//platform/common/logger/model:instance"],
    visibility = ["//visibility:public"],
)
