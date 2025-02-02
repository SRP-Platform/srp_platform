cc_test(
    name = "test_shm",
    srcs = [
        "shm_buffor_proxy_test.cc",
        "shm_buffor_skeleton_test.cc",
        "shm_proxy_test.cc",
        "shm_skeleton_test.cc",
    ],
    features = ["test_target"],
    deps = [
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//bindings/common/shm:shm_buffor_lib",
        "@srp_platform//bindings/common/shm:shm_lib",
        "@com_google_googletest//:gtest_main",
    ],
)
