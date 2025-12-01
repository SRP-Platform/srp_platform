load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application")

filegroup(
    name = "instance",
    srcs = [
        "example_app.json",
        ":service_instance",
    ],
    visibility = ["//example_app/ExampleApp/code:__subpackages__"],
)

filegroup(
    name = "service_instance",
    srcs = [
        "example_data_type.json",
        "example_data_type2.json",
        "example_service.json",
    ],
    visibility = ["//visibility:public"],
)

adaptive_application(
    name = "ExampleApp",
    bin = "//example_app/ExampleApp/code/application:ExampleApp",
    model_src = [":instance"],
    visibility = ["//visibility:public"],
)
