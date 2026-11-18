cc_binary(
    name = "ExampleApp",
    srcs = [
        "ExampleApp.cc",
        "ExampleApp.h",
        "main.cc",
    ],
    visibility = ["//example_app2/models/ExampleApp2:__pkg__", "//tests/mavlink:__pkg__"],
    # link_manager.cc uzywa std::ceil: na ARM (bbb-platform) libm trzeba podac jawnie (do poprawy w BUILD runtime'u)
    linkopts = ["-lm"],
    deps = [
        "//core/common:condition_lib",
        "//example_app2/ExampleApp2/code:ara",
        "//example_app2/ExampleApp2/code:someip_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/com/mavlink:link_transport_registry",
        "@srp_platform//ara/com/mavlink:serial_transport",
        "@srp_platform//ara/log",
    ],
)
