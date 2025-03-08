load("@srp_platform//tools/model_generator/platform:adaptive_application.bzl", "APPS_runtime_lib")

APPS_runtime_lib(
    name = "platform",
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
        ":platform",
        "@srp_platform//platform/exec:adaptive_application_lib",
        "@srp_platform//core/common:condition_lib",
        "@srp_platform//core/json:simba_json",
        "@srp_platform//platform/common/someip_demon/code/common/com/impl:multicast_controller",
        "@srp_platform//platform/common/someip_demon/code/sd:sd_controller",
        "@srp_platform//platform/common/someip_demon/code/common:controller",
        "@srp_platform//platform/common/someip_demon/code/connector",
        "@srp_platform//platform/common/someip_demon/code/db",
    ],
)
