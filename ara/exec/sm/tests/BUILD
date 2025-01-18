load("@srp_platform//tools/model_generator/ara:adaptive.bzl", "ara_json2sm_lib")
cc_library(
    name = "mock",
    testonly = True,
    srcs = [],
    hdrs = [
        "com_wrapper_mock.h",
        "state_mock.h",
    ],
    visibility = ["@srp_platform//ara/exec/sm/tests:__pkg__"],
    deps = [
        "@srp_platform//ara/exec/sm:sm_interfaces",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "State_controller_test",
    srcs = ["test_state_controller.cc"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/exec/sm:state_meager_lib",
        "@srp_platform//ara/exec/sm/tests:mock",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "Generated_state_controller_test",
    srcs = ["test_generated_state_controller.cc"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/exec/sm/tests:sm_lib",
        "@srp_platform//ara/exec/sm/tests:mock",
        "@com_google_googletest//:gtest_main",
    ],
)

filegroup(
    name = "sm_files",
    srcs = ["example_sm_model.json"],
)

ara_json2sm_lib(
    name = "sm_lib",
    src = "@srp_platform//ara/exec/sm/tests:sm_files",
    visibility = ["@srp_platform//ara/exec/sm/tests:__pkg__"]
)