load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "instance",
    srcs = [
        "diag_app.json",
        "@srp_platform//platform_definition/sm_service:SM_service",
    ],
    visibility = ["//visibility:private"],
)

json2model(
    name = "model",
    src = ["//apps/common/diag_demon/model:instance"],
    visibility = ["//visibility:private"],
)

model2com(
    name = "com_lib",
    src = "@srp_platform//apps/common/diag_demon/model",
    visibility = ["@srp_platform//apps/common/diag_demon/code:__subpackages__"],
)

model2runtime_env(
    name = "runtime_env",
    src = "@srp_platform//apps/common/diag_demon/model",
    visibility = ["@srp_platform//apps/common/diag_demon/code:__subpackages__"],
)

adaptive_application(
    name = "diag_demon",
    bin = "@srp_platform//apps/common/diag_demon/code/application:diag_demon",
    model_src = ["@srp_platform//apps/common/diag_demon/model"],
    # tar_path = "platform/",
    visibility = ["//visibility:public"],
)
