load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["@srp_platform//platform/common/someip_demon/model:instance"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "someip_demon",
    srcs = [
        "main.cc",
        "someip_demon.cc",
        "someip_demon.h",
    ],
    visibility = ["@srp_platform//platform/common/someip_demon:__subpackages__"],
    deps = [
        ":ara",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//platform/common/someip_demon/code/common/com/impl:multicast_controller",
        "@srp_platform//platform/common/someip_demon/code/sd:sd_controller",
        "@srp_platform//platform/common/someip_demon/code/common:controller",
        "@srp_platform//platform/common/someip_demon/code/connector",
        "@srp_platform//platform/common/someip_demon/code/db",
    ],
)
