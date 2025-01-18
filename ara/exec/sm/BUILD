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
