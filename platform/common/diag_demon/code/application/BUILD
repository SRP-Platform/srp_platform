cc_binary(
    name = "diag_demon",
    srcs = [
        "diag_demon.cc",
        "diag_demon.h",
        "main.cc",
    ],
    visibility = ["@srp_platform//platform/common/diag_demon:__subpackages__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//core/common:common_types",
        "@srp_platform//platform/common/diag_demon/model:ara",
        "@srp_platform//platform/common/diag_demon/code/services:example_did",
        "@srp_platform//platform/common/diag_demon/code/services:uds_server",
        "@srp_platform//platform/common/diag_demon/code/services/dtc",
    ],
)
