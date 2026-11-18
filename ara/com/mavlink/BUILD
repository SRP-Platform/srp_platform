# Build date of the binary: the lower bound of MAVLink signing timestamps on a system without a
# set clock. The genrule result is cached by bazel, so the value is the date of the build that
# last re-ran it.
genrule(
    name = "build_time",
    outs = ["mavlink_build_time.h"],
    cmd = "printf '#pragma once\\nconstexpr unsigned long long kBuildUnixTime = %su;\\n' \"$$(date +%s)\" > $@",
)

# Frames, codec, signing, SHA-256, the transport error domain and the shared log type.
cc_library(
    name = "mavlink_core",
    srcs = [
        "mavlink_codec.cc",
        "mavlink_frame.cc",
        "mavlink_signing.cc",
        "sha256.cc",
        "transport_error.cc",
    ],
    hdrs = [
        "mavlink_codec.h",
        "mavlink_frame.h",
        "mavlink_log.h",
        "mavlink_signing.h",
        "sha256.h",
        "transport_error.h",
        ":build_time",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core:error_lib",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

# ---------------------------------------------------------------- tests

cc_test(
    name = "test_mavlink_frame",
    srcs = ["ut/test_mavlink_frame.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_codec",
    srcs = ["ut/test_mavlink_codec.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_signing",
    srcs = ["ut/test_mavlink_signing.cc"],
    testonly = True,
    deps = [
        ":mavlink_core",
        "@com_google_googletest//:gtest_main",
    ],
)
