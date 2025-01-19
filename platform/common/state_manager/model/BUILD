load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_runtime_lib", "ara_someip_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["@srp_platform//platform/common/state_manager/model:instance"],
    visibility = ["@srp_platform//platform/common/state_manager/code:__subpackages__"],
)

ara_someip_lib(
    name = "someip_lib",
    model_src = ["@srp_platform//platform/common/state_manager/model:instance"],
    visibility = ["@srp_platform//platform/common/state_manager/code:__subpackages__"],
)

filegroup(
    name = "instance",
    srcs = [
        "sm_app.json",
        "@srp_platform//platform_definition/sm_service:SM_service",
        "@srp_platform//platform_definition/sm_service:sm_diag",
    ],
    visibility = ["@srp_platform//platform/common/state_manager/code:__subpackages__"],
)

adaptive_application(
    name = "state_manager",
    bin = "@srp_platform//platform/common/state_manager/code:state_manager",
    model_src = ["@srp_platform//platform/common/state_manager/model:instance"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
