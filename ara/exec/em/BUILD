load("@srp_platform//tools/model_generator/ara:data_structure_generator.bzl", "data_structure_generator")

cc_library(
    name = "execution_client_lib",
    hdrs = [
        "i_execution_client.h",
        "execution_client.h",
    ],
    srcs = ["execution_client.cc"],
    visibility = ["//visibility:public"],
    deps = [
        "//ara/core:Result",
        "//ara/exec/em:em_com_data",
        "//ara/com:com_controller",
    ],
)

data_structure_generator(
    name = "em_com_data",
    model = ["@srp_platform//platform_definition/data_structure:em_state_com_structure"],
    visibility = ["//visibility:public"],
)