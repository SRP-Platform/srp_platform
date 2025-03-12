cc_library(
    name = "adaptive_application_lib",
    srcs = [
        "adaptive_lifecycle_menager.cc",
    ],
    hdrs = [
        "adaptive_application.h",
        "adaptive_lifecycle.h",
        "adaptive_lifecycle_menager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/core:platform_initialization_lib",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//platform/log",
        # "@srp_platform//platform/com",
        "@srp_platform//platform/exec/em:execution_client_lib",
    ],
)
