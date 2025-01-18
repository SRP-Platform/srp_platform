cc_library(
    name = "dlt_service_lib",
    srcs = ["dlt_service.cc"],
    hdrs = ["dlt_service.h"],
    visibility = ["//platform/common/logger/code:__subpackages__"],
    deps = [
        "//bindings/common/socket:socket_ipc",
        "//platform/common/someip_demon/code/common:udp_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "//core/common:wait_queue",
        "//core/json:simba_json",
        "//platform/common/logger/code/data:dlt_data_types",
    ],
)
