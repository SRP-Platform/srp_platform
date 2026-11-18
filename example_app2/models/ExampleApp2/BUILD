load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application")

exports_files(["example_app.json"])

# Definicje usług (EngineService, ExampleService) są wspólne z example_app: obie aplikacje działają na jednym
# CPU (test_cpu/cpu/ec) i jednym MavlinkDaemonie, więc generator widzi jeden dialekt i jedną definicję usługi.
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
