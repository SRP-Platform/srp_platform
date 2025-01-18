cc_binary(
    name = "em",
    srcs = [
        "em_application.cc",
        "em_application.h",
        "main.cc",
    ],
    visibility = ["//platform/common/em:__subpackages__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//ara/log",
        "//core/common:condition_lib",
        "//core/common:wait_queue",
        "//core/json:simba_json",
        "//platform/common/em/model:ara",
        "//platform/common/em/code/services:IPC_Service",
        "//platform/common/em/code/services/em:em_service",
    ],
)
