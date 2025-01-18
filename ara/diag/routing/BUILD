cc_library(
    name = "routing",
    srcs = ["routable_uds_service.cc"],
    hdrs = ["routable_uds_service.h"],
    visibility = ["@srp_platform//ara/diag:__subpackages__"],
    deps = [
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core:instance_specifier",
        "@srp_platform//ara/core:model_db_lib",
        "@srp_platform//ara/diag:diag_error_domain",
        "@srp_platform//ara/log",
    ],
)

################################ ONLY FOR UT ################################
cc_library(
    name = "routing_for_ut",
    testonly = True,
    srcs = ["routable_uds_service.cc"],
    hdrs = ["routable_uds_service.h"],
    visibility = ["@srp_platform//ara/diag:__subpackages__"],
    deps = [
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/core:instance_specifier",
        "@srp_platform//ara/core:model_db_lib",
        "@srp_platform//ara/diag:diag_error_domain",
        "@srp_platform//ara/log:log_for_test",
    ],
)
