cc_library(
    name = "interfaces",
    hdrs = ["i_proxy_com_wrapper.h"],
    visibility = ["//ara/com/proxy:__subpackages__"],
    deps = ["@srp_platform//ara/com:msg_type"],
)

cc_library(
    name = "proxy",
    srcs = [
        "event.h",
        "proxy.h",
        "method.h",
        "method.cc",
        "fields.h",
        "proxy_handler.h",
        "event_packet_interpreter.h",
        "packet_interpreter.h",
        "proxy_packet_interpreter.h",
        "proxy_packet_interpreter.cc",
        "packet_interpreter.cc",
        "impl/method.h",
        "impl/fields.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com:instance_identifier_container",
        "@srp_platform//ara/com:msg_type",
        "@srp_platform//ara/com:types",
        "@srp_platform//ara/com/proxy:interfaces",
        "@srp_platform//ara/core:Result",
        "@srp_platform//core/data:data_conv_lib",
        "@srp_platform//ara/com:com_error_domain"
    ],
)
