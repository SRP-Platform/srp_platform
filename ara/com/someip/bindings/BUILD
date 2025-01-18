cc_library(
    name = "bindings_lib",
    srcs = [
        "binding_factory.cc",
        "ipc_bindings.cc",
        "proxy/udp_proxy_bindings.cc",
        "skeleton/udp_skeleton_bindings.cc",
    ],
    hdrs = [
        "binding_factory.h",
        "common_bindings.h",
        "ipc_bindings.h",
        "proxy/udp_proxy_bindings.h",
        "skeleton/udp_skeleton_bindings.h",
    ],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com:com_log",
        "@srp_platform//bindings/common/shm:shm_buffor_lib",
        "@srp_platform//ara/com/someip:someip_data",
        "@srp_platform//ara/com/someip/bindings:binding_interface",
        "@srp_platform//ara/com/someip/controller",
        "@srp_platform//ara/core:condition_variable",
        "@srp_platform//ara/core:string_view",
        "@srp_platform//ara/core/model:Model_lib",
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
        "@srp_platform//core/common:condition_lib",
    ],
)

cc_library(
    name = "binding_interface",
    hdrs = ["i_bindings.h"],
    visibility = ["@srp_platform//visibility:public"],
    deps = [
        "@srp_platform//ara/com/someip:someip_data",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core/model:Model_lib",
    ],
)
