cc_library(
    name = "skeleton",
    srcs = [
        "ipc_skeleton.cc",
        "ipc_skeleton_sd.cc",
        "ipc_skeleton_sd.h",
        "sd_event.cc",
        "sd_event.h",
        "sd_item.h",
    ],
    hdrs = ["ipc_skeleton.h"],
    data = [],
    visibility = ["//platform/com:__subpackages__"],
    deps = [
        "//bindings:log",
        "//bindings/com/ipc:thread_pool",
        "//bindings/common/socket:ipc_socket",
        "//core/common:wait_queue",
        "//platform/com:com_wrapper",
    ],
)
