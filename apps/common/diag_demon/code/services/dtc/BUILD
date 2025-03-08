load("@srp_platform//tools/model_generator/platform:data_structure_generator.bzl", "data_structure_generator")

data_structure_generator(
    name = "dtc_com_data",
    model = ["@srp_platform//APPS_definition/data_structure:dtc_com_data_structure"],
    visibility = ["//visibility:private"],
)

cc_library(
    name = "dtc",
    srcs = ["dtc_service.cc"],
    hdrs = ["dtc_service.h"],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
    deps = [
        ":dtc_interface",
        "@srp_platform//bindings/common/socket:ipc_socket",
        "@srp_platform//platform/log",
        ":dtc_com_data"
    ],
)

cc_library(
    name = "dtc_interface",
    hdrs = ["i_dtc_service.h"],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
    deps = [],
)
