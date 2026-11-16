load("//tools/pytest:pytest_test.bzl", "pytest_test")

pytest_test(
    name = "ping_test",
    srcs = ["ping_test.py"],
    deps = [
    ],
)

pytest_test(
    name = "ssh_test",
    srcs = ["ssh_test.py"],
    deps = [
    ],
)

pytest_test(
    name = "uds_test",
    srcs = ["uds_test.py"],
    deps = [
    ],
)

pytest_test(
    name = "uds_machine_state_did_test",
    srcs = ["uds_machine_state_did_test.py"],
    deps = [
    ],
)

cc_test(
    name = "em_execution_test",
    srcs = ["em_execution_test.cc"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "@srp_platform//ara/com:com_controller",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/exec/em:em_com_data",
        "@srp_platform//ara/exec/em:execution_client_lib",
        "@srp_platform//ara/log",
        "@srp_platform//platform/common/em/code/services/em:em_db",
        "@srp_platform//platform/common/em/code/services/em:em_service",
    ],
)
