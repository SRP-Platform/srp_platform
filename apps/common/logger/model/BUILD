load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

json2model(
    name="model",
    src = ["//apps/common/logger/model:instance"],
    visibility=["//visibility:private"]
)

model2com(
    name = "com_lib",
    src="//apps/common/logger/model",
    visibility=["//apps/common/em/code:__subpackages__"]
)
model2runtime_env(
    name = "runtime_env",
    src="//apps/common/logger/model",
    visibility=["//apps/common/logger/code:__subpackages__"]
)
filegroup(
    name = "instance",
    srcs = [
        "logger_app.json",
    ],
    visibility = ["@srp_platform//example_adaptive/ExampleApp/code:__subpackages__"],
)

adaptive_application(
    name = "logger",
    bin = "@srp_platform//apps/common/logger/code:logger",
    model_src = ["@srp_platform//apps/common/logger/model"],
    visibility = ["//visibility:public"],
)
