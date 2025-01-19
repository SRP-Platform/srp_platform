cc_library(
    name = "dlt_service_lib",
    srcs = ["dlt_service.cc"],
    hdrs = ["dlt_service.h"],
    visibility = ["@srp_platform//platform/common/logger/code:__subpackages__"],
    deps = [
        "@srp_platform//bindings/common/socket:socket_ipc",
        "@srp_platform//platform/common/someip_demon/code/common:udp_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//core/common:wait_queue",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//platform/common/logger/code/data:dlt_data_types",
    ],
)
