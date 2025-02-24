load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "instance",
    srcs = [
        "example_app.json",
        "example_service.json",
        "example_service_depl.json",
    ],
    visibility = ["//visibility:private"],
)

json2model(
    name = "model",
    src = ["//example/model:instance"],
    visibility = ["//visibility:private"],
)

model2com(
    name = "com_lib",
    src = "//example/model",
    visibility = ["//example/code:__subpackages__"],
)

model2runtime_env(
    name = "runtime_env",
    src = "//example/model",
    visibility = ["//example/code:__subpackages__"],
)

adaptive_application(
    name = "example_app",
    bin = "@srp_platform//example/code:example_app",
    model_src = ["@srp_platform//example/model"],
    visibility = ["//visibility:public"],
)
