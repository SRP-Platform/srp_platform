cc_binary(
    name = "diag_demon",
    srcs = [
        "diag_demon.cc",
        "diag_demon.h",
        "main.cc",
    ],
    visibility = ["//platform/common/diag_demon:__subpackages__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
        "//core/common:condition_lib",
        "//core/json:simba_json",
        "//core/common:common_types",
        "//platform/common/diag_demon/model:ara",
        "//platform/common/diag_demon/code/services:example_did",
        "//platform/common/diag_demon/code/services:uds_server",
        "//platform/common/diag_demon/code/services/dtc",
    ],
)
