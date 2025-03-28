cc_library(
    name = "log",
    srcs = [
        "logger.cpp",
        "logging_menager.cpp",
    ],
    hdrs = [
        "log.h",
        "logger.h",
        "logging_menager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/log:LogStream",
        "@srp_platform//platform/log:common",
        "@srp_platform//platform/log/sink:sink_lib",
    ],
)

cc_library(
    name = "common",
    hdrs = ["common.h"],
    visibility = ["@srp_platform//platform/log:__subpackages__"],
)

cc_library(
    name = "LogStream",
    srcs = ["log_stream.cpp"],
    hdrs = ["log_stream.h"],
    visibility = ["@srp_platform//platform/log:__subpackages__"],
    deps = [
        "@srp_platform//platform/core:error_lib",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//platform/log:common",
    ],
)

################################ ONLY FOR UT ################################
cc_library(
    name = "log_for_test",
    testonly = True,
    srcs = [
        "dummy/logging_menager.cpp",
        "logger.cpp",
    ],
    hdrs = [
        "log.h",
        "logger.h",
        "logging_menager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/log:LogStream",
        "@srp_platform//platform/log:common",
        "@srp_platform//platform/log/sink:sink_lib",
    ],
)
