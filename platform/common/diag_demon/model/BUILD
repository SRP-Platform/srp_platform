load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "ara_runtime_lib", "ara_someip_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["//platform/common/diag_demon/model:instance"],
    visibility = ["//visibility:public"],
)

ara_someip_lib(
    name = "someip_lib",
    model_src = ["//platform/common/diag_demon/model:instance"],
)

filegroup(
    name = "instance",
    srcs = [
        "diag_app.json",
        "//platform_definition/diag/jobs:read_vin_job",
    ],
)

adaptive_application(
    name = "diag_demon",
    bin = "//platform/common/diag_demon/code/application:diag_demon",
    model_src = ["//platform/common/diag_demon/model:instance"],
    visibility = ["//visibility:public"],
)
