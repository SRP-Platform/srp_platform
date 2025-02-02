cc_binary(
    name = "ipc_service_discover",
    srcs = [
        "ipc_service_discover.cc",
        "ipc_service_discover.h",
        "main.cc",
    ],
    visibility = ["//apps/common/ipc_service_discover:__subpackages__"],
    deps = [
        "@srp_platform//platform/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//apps/common/ipc_service_discover/model:runtime_env",
        "@srp_platform//bindings/common/socket:ipc_socket"
    ],
)
