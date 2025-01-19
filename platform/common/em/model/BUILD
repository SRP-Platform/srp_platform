load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_runtime_lib", "ara_someip_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["@srp_platform//platform/common/em/model:instance"],
    visibility = ["@srp_platform//platform/common/em/code:__subpackages__"],
)

ara_someip_lib(
    name = "someip_lib",
    model_src = ["@srp_platform//platform/common/em/model:instance"],
    visibility = ["@srp_platform//platform/common/em/code:__subpackages__"],
)

filegroup(
    name = "instance",
    srcs = [
        "em_app.json",
        "@srp_platform//platform_definition/sm_service:SM_service",
    ],
    visibility = ["//visibility:private"],
)

adaptive_application(
    name = "em",
    bin = "@srp_platform//platform/common/em/code:em",
    model_src = ["@srp_platform//platform/common/em/model:instance"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
