cc_library(
    name = "uds_server",
    srcs = [
        "uds_server.cc",
        "validator_controller.cc",
    ],
    hdrs = [
        "uds_server.h",
        "validator_controller.h",
    ],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
    deps = [
        "@srp_platform//platform/log",
        "@srp_platform//libdoip/libdoipserver",
        "@srp_platform//platform/common/diag_demon/code/services:uds_data_types",
        "@srp_platform//platform/common/diag_demon/code/services:uds_interfaces",
        "@srp_platform//bindings/common/socket:stream_socket_ipc",
    ],
)

cc_library(
    name = "example_did",
    hdrs = ["example_did.h"],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
    deps = [
        "@srp_platform//platform/log",
        "@srp_platform//platform/diag:uds_lib",
    ],
)

cc_library(
    name = "uds_data_types",
    hdrs = [
        "uds_nrc_codes.h",
        "uds_request.h",
    ],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
)

cc_library(
    name = "uds_interfaces",
    hdrs = ["i_validator.h"],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
    deps = [":uds_data_types"],
)
