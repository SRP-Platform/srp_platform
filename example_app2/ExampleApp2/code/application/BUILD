# The application is a library plus a thin binary so that //tests/mavlink can run it with its own
# main (no Execution Management on the host).
cc_library(
    name = "example_app_lib",
    srcs = ["ExampleApp.cc"],
    hdrs = ["ExampleApp.h"],
    visibility = ["//tests/mavlink:__pkg__"],
    deps = [
        "//core/common:condition_lib",
        "//example_app2/ExampleApp2/code:ara",
        "//example_app2/ExampleApp2/code:someip_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
    ],
)

cc_binary(
    name = "ExampleApp",
    srcs = ["main.cc"],
    visibility = ["//example_app2/models/ExampleApp2:__pkg__"],
    deps = [
        ":example_app_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
    ],
)
