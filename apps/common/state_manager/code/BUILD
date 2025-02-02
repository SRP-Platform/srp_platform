load("@srp_platform//tools/model_generator/platform:adaptive.bzl", "platform_json2sm_lib")

cc_library(
    name = "diag_lib",
    hdrs = ["did_impl.h"],
    visibility = ["@srp_platform//platform/common/diag_demon/code:__subpackages__"],
    deps = [
        "@srp_platform//platform/com:com_error_domain",
        "@srp_platform//platform/diag:uds_lib",
        # "@srp_platform//platform/exec/sm:sm_interfaces",
        "@srp_platform//platform/log",
    ],
)

platform_json2sm_lib(
    name = "sm_lib",
    src = "@srp_platform//platform_definition/sm:example_config",
)

cc_binary(
    name = "state_manager",
    srcs = [
        "com_wrapper.h",
        "main.cc",
        "state_manager.cc",
        "state_manager.h",
    ],
    visibility = ["@srp_platform//apps/common/state_manager/model:__subpackages__"],
    deps = [
        "@srp_platform//platform/exec:adaptive_application_lib",
        "@srp_platform//core/json:simba_json",
        "//core/common:condition_lib",
        "@srp_platform//apps/common/state_manager/model:runtime_env",
        "@srp_platform//apps/common/state_manager/model:com_lib",
        "@srp_platform//apps/common/state_manager/code:diag_lib",
        "@srp_platform//apps/common/state_manager/code:sm_lib",
    ],
)
