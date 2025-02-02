cc_library(
    name = "interfaces",
    hdrs = ["i_skeleton_com_wrapper.h"],
    visibility = ["//platform/com/skeleton:__subpackages__"],
    deps = [
        "@srp_platform//platform/com:com_wrapper",
        "@srp_platform//platform/com:msg_type",
    ],
)

cc_library(
    name = "skeleton",
    srcs = [
        "event.h",
        "impl/method.h",
        "method.cc",
        "method.h",
        "method_packet_interpreter.h",
        "packet_interpreter.cc",
        "packet_interpreter.h",
        "skeleton.h",
        "skeleton_packet_interpreter.cc",
        "skeleton_packet_interpreter.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//core/data:data_conv_lib",
        "@srp_platform//platform/com:com_error_domain",
        "@srp_platform//platform/com:com_log",
        "@srp_platform//platform/com:com_wrapper",
        "@srp_platform//platform/com:instance_identifier_container",
        "@srp_platform//platform/com:msg_type",
        "@srp_platform//platform/com:types",
        "@srp_platform//platform/com/internal",
        "@srp_platform//platform/com/skeleton:interfaces",
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:model_db_lib",
        "//platform/com/internal:internal_controller",
    ],
)
