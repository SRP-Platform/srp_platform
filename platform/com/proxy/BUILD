cc_library(
    name = "interfaces",
    hdrs = ["i_proxy_com_wrapper.h"],
    visibility = ["//platform/com/proxy:__subpackages__"],
    deps = ["@srp_platform//platform/com:msg_type"],
)

cc_library(
    name = "proxy",
    srcs = [
        "event.h",
        "event_packet_interpreter.h",
        "fields.h",
        "impl/fields.h",
        "impl/method.h",
        "method.cc",
        "method.h",
        "packet_interpreter.cc",
        "packet_interpreter.h",
        "proxy.h",
        "proxy_handler.h",
        "proxy_packet_interpreter.cc",
        "proxy_packet_interpreter.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/com:com_error_domain",
        "@srp_platform//platform/com:com_log",
        "@srp_platform//platform/com:instance_identifier_container",
        "@srp_platform//platform/com:msg_type",
        "@srp_platform//platform/com:types",
        "@srp_platform//platform/com/internal",
        "@srp_platform//platform/com/proxy:interfaces",
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:model_db_lib",
        "@srp_platform//core/data:data_conv_lib",
    ],
)
