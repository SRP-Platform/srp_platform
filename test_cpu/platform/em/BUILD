load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "platform_runtime_lib", "platform_someip_lib")

platform_runtime_lib(
    name = "platform",
    model_src = ["//deployment/platform/em:instance"],
    visibility = ["//platform/common/em/code:__subpackages__"],
)

platform_someip_lib(
    name = "someip_lib",
    model_src = ["//deployment/platform/em:instance"],
    visibility = ["//platform/common/em/code:__subpackages__"],
)

filegroup(
    name = "instance",
    srcs = [
        "em_app.json",
        "//deployment/system_definition/platform/sm_service:SM_service",
    ],
    visibility = ["//visibility:private"],
)

adaptive_application(
    name = "em",
    bin = "//platform/common/em/code:em",
    model_src = ["//deployment/platform/em:instance"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
