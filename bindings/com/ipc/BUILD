cc_library(
    name = "ipc",
    srcs = [
        "ipc_controller.cc",
        "ipc_controller.h",
    ],
    visibility = [
        "//bindings/com:__subpackages__",
        "//platform/com:__subpackages__",
    ],
    deps = [
        "//bindings:log",
        "//bindings/com/ipc:handlers",
        "//bindings/common/controller:interfaces",
        "//platform/com:msg_type",
    ],
)

cc_library(
    name = "handlers",
    hdrs = ["handler.h"],
    visibility = ["//bindings/com:__subpackages__"],
    deps = [
        "//bindings:log",
        "//platform/com:msg_type",
    ],
)

cc_library(
    name = "thread_pool",
    hdrs = ["com_thread_pool.h"],
    visibility = ["//bindings/com/ipc:__subpackages__"],
    deps = [
        "@boost//:asio",
        "@boost//:thread",
    ],
)
