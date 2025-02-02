cc_binary(
    name = "logger",
    srcs = [
        "main.cc",
    ],
    visibility = ["@srp_platform//apps/common/logger:__subpackages__"],
    deps = [
        "@srp_platform//apps/common/logger/model:runtime_env",
        "@srp_platform//apps/common/logger/code/application:dlt_service_lib",
    ],
)
