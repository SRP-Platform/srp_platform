load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application")

exports_files(["example_app.json"])

# The service definitions (EngineService, ExampleService) are shared with example_app: both
# applications run on one CPU (test_cpu/cpu/ec) with one MavlinkDaemon, so the generator reads
# one dialect and one definition per service. A second copy of the same service in this
# directory would be a duplicate definition. ExampleApp2 is the client of the EngineService that
# ExampleApp offers.
filegroup(
    name = "instance",
    srcs = [
        "example_app.json",
        ":service_instance",
    ],
    visibility = ["//example_app2/ExampleApp2/code:__subpackages__"],
)

filegroup(
    name = "service_instance",
    srcs = ["//example_app/models/ExampleApp:service_instance"],
    visibility = ["//visibility:public"],
)

adaptive_application(
    name = "ExampleApp2",
    bin = "//example_app2/ExampleApp2/code/application:ExampleApp",
    model_src = [":instance"],
    visibility = ["//visibility:public"],
)
