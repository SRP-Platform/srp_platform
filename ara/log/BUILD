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
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/log:LogStream",
        "@srp_platform//ara/log:common",
        "@srp_platform//ara/log/sink:sink_lib",
    ],
)

cc_library(
    name = "common",
    hdrs = ["common.h"],
    visibility = ["@srp_platform//ara/log:__subpackages__"],
)

cc_library(
    name = "LogStream",
    srcs = ["log_stream.cpp"],
    hdrs = ["log_stream.h"],
    visibility = ["@srp_platform//ara/log:__subpackages__"],
    deps = [
        "@srp_platform//ara/core:error_lib",
        "@srp_platform//ara/core:instance_specifier",
        "@srp_platform//ara/log:common",
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
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/log:LogStream",
        "@srp_platform//ara/log:common",
        "@srp_platform//ara/log/sink:sink_lib",
    ],
)
