load("//tools/model_generator/ara:adaptive_application.bzl", "json2model", "model2com", "model2runtime_env")

filegroup(
    name = "srcs",
    srcs = [
        "example_app.json",
        "example_service.json",
        "example_service_depl.json",
    ],
)

json2model(
    name = "test",
    src = ["//tools/model_generator/v2/example:srcs"],
    component_name = "srp.example.ExampleApp"
)

model2com(
    name = "com_lib",
    src = "//tools/model_generator/v2/example:test"
)
cc_binary(
    name = "test_a",
    deps = ["//tools/model_generator/v2/example:com_lib"],
    srcs = ["miain.cpp"],
)

model2runtime_env(name = "runtime_lib", src = "//tools/model_generator/v2/example:test")