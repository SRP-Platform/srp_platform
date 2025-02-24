cc_binary(
    name = "example_app",
    srcs = [
        "main.cc",
        "example_app.cc",
        "example_app.h"
    ],
    visibility = ["//example/model:__subpackages__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//example/model:runtime_env",
        "@srp_platform//example/model:com_lib"
    ],
)
