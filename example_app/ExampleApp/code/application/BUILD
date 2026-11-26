# The application is a library plus a thin binary so that //tests/mavlink can run it with its own
# main (no Execution Management on the host).
cc_library(
    name = "example_app_lib",
    srcs = [
        "ExampleApp.cc",
        "example_did.h",
        "example_service.h",
    ],
    hdrs = [
        "ExampleApp.h",
        "engine_service.h",
    ],
    visibility = ["//tests/mavlink:__pkg__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
        "//core/common:condition_lib",
        "//example_app/ExampleApp/code:ara",
        "//example_app/ExampleApp/code:someip_lib",
    ],
)

cc_binary(
    name = "ExampleApp",
    srcs = ["main.cc"],
    visibility = ["//example_app/models/ExampleApp:__pkg__"],
    deps = [
        ":example_app_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
    ],
)
