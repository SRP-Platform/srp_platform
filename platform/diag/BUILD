load("@srp_platform//tools/model_generator/platform:data_structure_generator.bzl", "data_structure_generator")

data_structure_generator(
    name = "dtc_com_data",
    model = ["@srp_platform//platform_definition/data_structure:dtc_com_data_structure"],
    visibility = ["//visibility:private"],
)

cc_library(
    name = "monitor",
    srcs = ["monitor.cc"],
    hdrs = [
        "monitor.h",
        "monitor_types.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//platform/core:model_db_lib",
        "@srp_platform//platform/diag:diagnostic_menager",
        "@srp_platform//core/common:condition_lib",
    ],
)

cc_library(
    name = "diag_error_domain",
    srcs = [
        "diag_error_domain.cc",
        "uds_error_domain.cc",
    ],
    hdrs = [
        "diag_error_domain.h",
        "uds_error_domain.h",
    ],
    visibility = ["@srp_platform//platform/diag:__subpackages__"],
    deps = ["@srp_platform//platform/core:error_lib"],
)

cc_library(
    name = "uds_lib",
    srcs = [
        "generic_data_identifier.cc",
        "generic_routine.cc",
    ],
    hdrs = [
        "generic_data_identifier.h",
        "generic_routine.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/diag:diag_error_domain",
        "@srp_platform//platform/diag/routing",
    ],
)

cc_library(
    name = "diagnostic_menager",
    srcs = ["diagnostic_menager.cc"],
    hdrs = ["diagnostic_menager.h"],
    visibility = ["@srp_platform//platform:__subpackages__"],
    deps = [
        "@srp_platform//platform/com:com_controller",
        "@srp_platform//platform/diag:diag_error_domain",
        "@srp_platform//platform/diag:dtc_com_data",
    ],
)
