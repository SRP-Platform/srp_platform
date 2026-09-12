load("@srp_platform//tools/model_generator/ara:data_structure_generator.bzl", "data_structure_generator")

cc_library(
    name = "execution_client_lib",
    srcs = ["execution_client.cc"],
    hdrs = [
        "execution_client.h",
        "i_execution_client.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":em_com_data",
        "@srp_platform//ara/com:com_controller",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/log",
    ],
)

data_structure_generator(
    name = "em_com_data",
    model = ["@srp_platform//platform_definition/data_structure:em_state_com_structure"],
    visibility = ["//visibility:public"],
)
