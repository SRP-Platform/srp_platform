load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "platform_runtime_lib", "platform_someip_lib")

platform_runtime_lib(
    name = "platform",
    model_src = ["//deployment/platform/state_manager:instance"],
    visibility = ["//platform/common/state_manager/code:__subpackages__"],
)

platform_someip_lib(
    name = "someip_lib",
    model_src = ["//deployment/platform/state_manager:instance"],
    visibility = ["//platform/common/state_manager/code:__subpackages__"],
)

filegroup(
    name = "instance",
    srcs = [
        "sm_app.json",
        "//deployment/system_definition/platform/sm_service:SM_service",
        "//deployment/system_definition/platform/sm_service:sm_diag",
    ],
    visibility = ["//platform/common/state_manager/code:__subpackages__"],
)

adaptive_application(
    name = "state_manager",
    bin = "//platform/common/state_manager/code:state_manager",
    model_src = ["//deployment/platform/state_manager:instance"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
