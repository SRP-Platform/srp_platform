load("@srp_platform//tools/model_generator/ara:data_structure_generator.bzl", "data_structure_generator")

cc_library(
    name = "sm_interfaces",
    hdrs = [
        "i_sm_com_wrapper.h",
        "i_state.h",
        "i_state_controller.h",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "state_meager_lib",
    srcs = ["state_controller.cc"],
    hdrs = [
        "state.h",
        "state_controller.h",
    ],
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//ara/exec/sm:sm_interfaces"],
)

filegroup(
    name = "sm",
    srcs = ["initialization.h"],
    visibility = ["//visibility:public"],
)

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
        "//ara/exec/sm:em_com_data",
        "//bindings/common/socket:socket_ipc",
    ],
)

data_structure_generator(
    name = "em_com_data",
    model = ["@srp_platform//platform_definition/data_structure:em_state_com_structure"],
    visibility = ["//visibility:public"],
)