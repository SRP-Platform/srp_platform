cc_library(
    name = "ipc_socket",
    srcs = ["proccess_socket.cc"],
    hdrs = ["proccess_socket.h"],
    data = [],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/com:com_error_domain",
        "@srp_platform//platform/core:Result",
    ],
)

cc_library(
    name = "stream_socket_ipc",
    srcs = [
        "stream_ipc_socket.cc",
    ],
    hdrs = [
        "Isocket_stream.h",
        "stream_ipc_socket.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/com:com_error_domain",
        "@srp_platform//platform/core:Result",
    ],
)

cc_library(
    name = "socket_ipc",
    srcs = [
        "ipc_socket.cc",
    ],
    hdrs = [
        "Isocket.h",
        "ipc_socket.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/com:com_error_domain",
        "@srp_platform//platform/core:Result",
    ],
)
