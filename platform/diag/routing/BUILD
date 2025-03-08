cc_library(
    name = "routing",
    srcs = ["routable_uds_service.cc"],
    hdrs = ["routable_uds_service.h"],
    visibility = ["@srp_platform//platform/diag:__subpackages__"],
    deps = [
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//platform/core:model_db_lib",
        "@srp_platform//platform/diag:diag_error_domain",
        "@srp_platform//platform/log",
    ],
)

################################ ONLY FOR UT ################################
cc_library(
    name = "routing_for_ut",
    testonly = True,
    srcs = ["routable_uds_service.cc"],
    hdrs = ["routable_uds_service.h"],
    visibility = ["@srp_platform//platform/diag:__subpackages__"],
    deps = [
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//platform/core:model_db_lib",
        "@srp_platform//platform/diag:diag_error_domain",
        "@srp_platform//platform/log:log_for_test",
    ],
)
