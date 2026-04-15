cc_binary(
    name = "ExampleApp",
    srcs = [
        "ExampleApp.cc",
        "ExampleApp.h",
        "main.cc",
    ],
    visibility = ["//example_app2/models/ExampleApp2:__pkg__"],
    deps = [
        "//core/common:condition_lib",
        "//example_app2/ExampleApp2/code:ara",
        "//example_app2/ExampleApp2/code:someip_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
    ],
)
