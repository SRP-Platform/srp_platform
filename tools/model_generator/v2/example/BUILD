load("//tools/model_generator/platform:adaptive_application.bzl", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "srcs",
    srcs = [
        "cpu_model.json",
        "example_app.json",
        "example_did_depl.json",
        "example_service.json",
        "example_service_depl.json",
        "//platform_definition/common:DiD",
    ],
)

json2model(
    name = "test",
    src = ["//tools/model_generator/v2/example:srcs"],
    component_name = "srp.example.ExampleApp",
)

model2com(
    name = "com_lib",
    src = "//tools/model_generator/v2/example:test",
)

cc_binary(
    name = "test_a",
    srcs = ["miain.cpp"],
    deps = ["//tools/model_generator/v2/example:com_lib"],
)

model2runtime_env(
    name = "runtime_lib",
    src = "//tools/model_generator/v2/example:test",
)
