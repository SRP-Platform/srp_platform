load("@srp_platform//tools/model_generator/ara:adaptive.bzl", "ara_json2sm_lib")

cc_library(
    name = "diag_lib",
    hdrs = ["did_impl.h"],
    visibility = ["//platform/common/diag_demon/code:__subpackages__"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/diag:uds_lib",
        # "@srp_platform//ara/exec/sm:sm_interfaces",
        "@srp_platform//ara/log",
    ],
)

ara_json2sm_lib(
    name = "sm_lib",
    src = "//platform_definition/sm:example_config",
)

cc_binary(
    name = "state_manager",
    srcs = [
        "com_wrapper.h",
        "main.cc",
        "state_manager.cc",
        "state_manager.h",
    ],
    visibility = ["//platform/common/state_manager/model:__subpackages__"],
    deps = [
        "@srp_platform//ara/exec:adaptive_application_lib",
        # "@srp_platform//ara/exec/sm:sm_interfaces",
        "//core/json:simba_json",
        "//platform/common/state_manager/model:ara",
        "//platform/common/state_manager/model:someip_lib",
        "//platform/common/state_manager/code:diag_lib",
        "//platform/common/state_manager/code:sm_lib",
    ],
)
