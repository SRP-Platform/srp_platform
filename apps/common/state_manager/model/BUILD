load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "instance",
    srcs = [
        "sm_app.json",
        "@srp_platform//platform_definition/sm_service:SM_service",
    ],
    visibility = ["//visibility:private"],
)

json2model(
    name="model",
    src = ["//apps/common/state_manager/model:instance"],
    visibility=["//visibility:private"]
)

model2com(
    name = "com_lib",
    src="//apps/common/state_manager/model",
    visibility=["@srp_platform//apps/common/state_manager/code:__subpackages__"]
)
model2runtime_env(
    name = "runtime_env",
    src="//apps/common/state_manager/model",
    visibility = ["@srp_platform//apps/common/state_manager/code:__subpackages__"],
)
adaptive_application(
    name = "state_manager",
    bin = "@srp_platform//apps/common/state_manager/code:state_manager",
    model_src = ["@srp_platform//apps/common/state_manager/model"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
