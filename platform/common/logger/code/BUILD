cc_binary(
    name = "logger",
    srcs = [
        "main.cc",
    ],
    visibility = ["@srp_platform//platform/common/logger:__subpackages__"],
    deps = [
        "@srp_platform//platform/common/logger/model:ara",
        "@srp_platform//platform/common/logger/code/application:dlt_service_lib",
    ],
)
