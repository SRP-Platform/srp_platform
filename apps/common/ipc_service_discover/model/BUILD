load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "instance",
    srcs = [
        "ipc_service_discover.json",
        "@srp_platform//platform_definition/sm_service:SM_service",
    ],
    visibility = ["//visibility:private"],
)

json2model(
    name="model",
    src = ["//apps/common/ipc_service_discover/model:instance"],
    visibility=["//visibility:private"]
)

model2com(
    name = "com_lib",
    src="//apps/common/ipc_service_discover/model",
    visibility=["//apps/common/ipc_service_discover/code:__subpackages__"]
)
model2runtime_env(
    name = "runtime_env",
    src="//apps/common/ipc_service_discover/model",
    visibility=["//apps/common/ipc_service_discover/code:__subpackages__"]
)
adaptive_application(
    name = "ipc_service_discover",
    bin = "@srp_platform//apps/common/ipc_service_discover/code:ipc_service_discover",
    model_src = ["@srp_platform//apps/common/ipc_service_discover/model"],
    tar_path = "platform/",
    visibility = ["//visibility:public"],
)
