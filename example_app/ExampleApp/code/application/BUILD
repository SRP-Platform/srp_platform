cc_binary(
    name = "ExampleApp",
    srcs = [
        "ExampleApp.cc",
        "ExampleApp.h",
        "example_did.h",
        "example_service.h",
        "main.cc",
    ],
    visibility = ["//example_app/models/ExampleApp:__pkg__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
        "//core/common:condition_lib",
        "//example_app/ExampleApp/code:ara",
        "//example_app/ExampleApp/code:someip_lib",
    ],
)
