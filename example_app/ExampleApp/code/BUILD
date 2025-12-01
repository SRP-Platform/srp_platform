load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib", "ara_someip_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["//example_app/models/ExampleApp:instance"],
    visibility = ["//example_app/ExampleApp/code:__subpackages__"],
)

ara_someip_lib(
    name = "someip_lib",
    model_src = ["//example_app/models/ExampleApp:instance"],
    visibility = ["//example_app/ExampleApp/code:__subpackages__"],
)
