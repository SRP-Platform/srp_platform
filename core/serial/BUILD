cc_library(
    name = "serial_port",
    srcs = ["serial_port.cc"],
    hdrs = ["serial_port.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "test_serial_port",
    srcs = ["ut/test_serial_port.cc"],
    testonly = True,
    deps = [
        ":serial_port",
        "@com_google_googletest//:gtest_main",
    ],
)
