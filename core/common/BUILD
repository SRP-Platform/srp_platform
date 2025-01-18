cc_library(
    name = "core",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@srp_platform//core/common:common_types",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//core/common:common_converter",
        "@srp_platform//core/common:crc_lib",
        "@srp_platform//core/common:wait_queue",
    ]
)

cc_library(
    name = "common_types",
    hdrs = ["error_code.h"],
    visibility = [
        "//visibility:public",
    ],
)
cc_library(
    name = "condition_lib",
    hdrs = ["condition.h"],
    srcs = ["condition.cc"],
    visibility = ["//visibility:public"],
)
cc_library(
    name = "common_converter",
    hdrs = ["endianess_converter.h"],
    visibility = [
        "@srp_platform//core:__subpackages__",
        "@srp_platform//communication-core:__subpackages__",
    ],
)

cc_library(
    name = "crc_lib",
    srcs = ["crc_16.cc"],
    hdrs = ["crc_16.h"],
    visibility = [
        "@srp_platform//core/test:__subpackages__",
        ],
)

cc_library(
    name = "wait_queue",
    hdrs = ["wait_queue.h"],
    visibility = ["//visibility:public"],
)
