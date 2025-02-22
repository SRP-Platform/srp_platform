load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "instance",
    srcs = [
        "em_app.json",
        "@srp_platform//platform_definition/sm_service:SM_service",
    ],
    visibility = ["//visibility:private"],
)

json2model(
    name="model",
    src = ["//platform/common/em/model:instance"],
    visibility=["//visibility:private"]
)

model2com(
    name = "com_lib",
    src="//platform/common/em/model",
    visibility=["//platform/common/em/code:__subpackages__"]
)
model2runtime_env(
    name = "runtime_env",
    src="//platform/common/em/model",
    visibility=["//platform/common/em/code:__subpackages__"]
)
adaptive_application(
    name = "em",
    bin = "@srp_platform//platform/common/em/code:em",
    model_src = ["@srp_platform//platform/common/em/model"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
