cc_library(
    name = "transport_error",
    srcs = ["transport_error.cc"],
    hdrs = ["transport_error.h"],
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//ara/core:error_lib"],
)

# Interfejs transportu bajtów: stabilny kontrakt platforma <-> aplikacja/srp.
cc_library(
    name = "byte_transport",
    hdrs = ["i_byte_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":transport_error",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "serial_transport",
    srcs = ["serial_transport.cc"],
    hdrs = ["serial_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":byte_transport",
        ":transport_error",
        "@srp_platform//core/serial:serial_port",
    ],
)

cc_library(
    name = "link_transport_registry",
    srcs = ["link_transport_registry.cc"],
    hdrs = ["link_transport_registry.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":byte_transport",
        ":transport_error",
        "@srp_platform//ara/core:model_db_lib",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

cc_library(
    name = "sha256",
    srcs = ["sha256.cc"],
    hdrs = ["sha256.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mavlink_frame",
    srcs = ["mavlink_frame.cc"],
    hdrs = ["mavlink_frame.h"],
    visibility = ["//visibility:public"],
    deps = [":sha256"],
)

cc_library(
    name = "mavlink_codec",
    srcs = ["mavlink_codec.cc"],
    hdrs = ["mavlink_codec.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_frame",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

cc_library(
    name = "mavlink_signing",
    srcs = ["mavlink_signing.cc"],
    hdrs = ["mavlink_signing.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":mavlink_frame",
        ":transport_error",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "mavlink_link",
    srcs = ["mavlink_link.cc"],
    hdrs = ["mavlink_link.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":byte_transport",
        ":mavlink_frame",
        ":mavlink_signing",
        ":transport_error",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
    ],
)

cc_library(
    name = "link_manager",
    srcs = ["link_manager.cc"],
    hdrs = [
        "dedup_table.h",
        "link_manager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":byte_transport",
        ":link_transport_registry",
        ":mavlink_codec",
        ":mavlink_link",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)

# LinkManager::Instance() z logowaniem przez ara::log (bez niego testy nie linkują ara::log).
cc_library(
    name = "link_manager_global",
    srcs = ["link_manager_instance.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":link_manager",
        "@srp_platform//ara/com:com_log",
        "@srp_platform//ara/log",
    ],
)

# ---------------------------------------------------------------- testonly

cc_library(
    name = "memory_transport",
    testonly = True,
    hdrs = ["testing/memory_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":byte_transport",
        ":transport_error",
    ],
)

# Wspólny zestaw testów kontraktu IByteTransport (do odpalenia także w srp).
cc_library(
    name = "byte_transport_contract_test",
    testonly = True,
    srcs = ["testing/byte_transport_contract_test.cc"],
    hdrs = ["testing/byte_transport_contract_test.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":byte_transport",
        ":transport_error",
        "@com_google_googletest//:gtest",
    ],
    alwayslink = True,
)

cc_test(
    name = "test_transport_error",
    srcs = ["ut/test_transport_error.cc"],
    testonly = True,
    deps = [
        ":transport_error",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_link_transport_registry",
    srcs = ["ut/test_link_transport_registry.cc"],
    testonly = True,
    deps = [
        ":link_transport_registry",
        ":memory_transport",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_serial_transport",
    srcs = ["ut/test_serial_transport.cc"],
    testonly = True,
    deps = [
        ":byte_transport_contract_test",
        ":serial_transport",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_memory_transport",
    srcs = ["ut/test_memory_transport.cc"],
    testonly = True,
    deps = [
        ":byte_transport_contract_test",
        ":memory_transport",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_frame",
    srcs = ["ut/test_mavlink_frame.cc"],
    testonly = True,
    deps = [
        ":mavlink_frame",
        ":sha256",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_codec",
    srcs = ["ut/test_mavlink_codec.cc"],
    testonly = True,
    deps = [
        ":mavlink_codec",
        ":mavlink_frame",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_signing",
    srcs = ["ut/test_mavlink_signing.cc"],
    testonly = True,
    deps = [
        ":mavlink_signing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_mavlink_link",
    srcs = ["ut/test_mavlink_link.cc"],
    testonly = True,
    deps = [
        ":mavlink_link",
        ":memory_transport",
        "@com_google_googletest//:gtest_main",
        "@srp_platform//ara/com:com_error_domain",
    ],
)

cc_test(
    name = "test_dedup_table",
    srcs = ["ut/test_dedup_table.cc"],
    testonly = True,
    deps = [
        ":link_manager",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_link_manager",
    srcs = ["ut/test_link_manager.cc"],
    testonly = True,
    deps = [
        ":link_manager",
        ":memory_transport",
        "@com_google_googletest//:gtest_main",
        "@srp_platform//ara/com:com_error_domain",
    ],
)
