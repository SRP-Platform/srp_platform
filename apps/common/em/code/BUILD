cc_binary(
    name = "em",
    srcs = [
        "em_application.cc",
        "em_application.h",
        "main.cc",
    ],
    visibility = ["@srp_platform//apps/common/em:__subpackages__"],
    deps = [
        "@srp_platform//platform/exec:adaptive_application_lib",
        "@srp_platform//platform/log",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//core/common:wait_queue",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//apps/common/em/model:runtime_env",
        # "@srp_platform//apps/common/em/code/services:IPC_Service",
        "@srp_platform//apps/common/em/code/services/em:em_service",
    ],
)
