cc_library(
    name = "proxy",
    srcs = ["ipc_proxy.cc"],
    hdrs = [
        "ipc_proxy.h",
        "ipc_service_discover_proxy.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//bindings:log",
        "//bindings/com:interfaces",
        "//bindings/com/ipc",
        "//bindings/com/ipc:handlers",
        "//bindings/com/ipc:thread_pool",
        "//core/common:condition_lib",
        "//platform/com:com_wrapper",
    ],
)
