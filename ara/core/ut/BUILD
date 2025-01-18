cc_test(
    name = "benchmarks",
    srcs = ["result_benchmark.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:Result",
        "@com_google_benchmark//:benchmark_main",
    ],
)

cc_test(
    name = "test_error_code",
    srcs = ["test_error_code.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:error_lib",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)

cc_test(
    name = "test_core_error_domain",
    srcs = ["test_core_error_domain.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:core_error_lib",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)

cc_test(
    name = "test_instance_specifire",
    srcs = ["test_instance_specifire.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:instance_specifier",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)
cc_test(
    name = "test_result",
    srcs = ["test_result.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:Result",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)

cc_test(
    name = "test_model_item",
    srcs = ["test_model_item.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:model_db_lib",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)
cc_test(
    name = "test_model_db",
    srcs = ["test_model_db.cc"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:model_db_lib",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)
cc_test(
    name = "test_condition_variable",
    srcs = [
        "test_condition_variable.cc",
        ],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/core:condition_variable",
        "@com_google_googletest//:gtest_main",
    ],
    testonly = True,
)