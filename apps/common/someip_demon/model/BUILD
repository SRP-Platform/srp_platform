load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

json2model(
    name="model",
    src = ["//apps/common/someip_demon/model:instance"],
    visibility=["//visibility:private"]
)

model2com(
    name = "com_lib",
    src="//apps/common/someip_demon/model",
    visibility=["//apps/common/someip_demon:__subpackages__"]
)
model2runtime_env(
    name = "runtime_env",
    src="//apps/common/someip_demon/model",
    visibility=["//apps/common/someip_demon:__subpackages__"]
)
filegroup(
    name = "instance",
    srcs = [
        "someip_app.json",
    ],
    visibility = ["//apps/common/someip_demon:__subpackages__"],
)

adaptive_application(
    name = "someip_demon",
    bin = "@srp_platform//apps/common/someip_demon/code/application:someip_demon",
    model_src = ["//apps/common/someip_demon/model"],
    visibility = ["//visibility:public"],
)
