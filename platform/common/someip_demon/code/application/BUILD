load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

ara_runtime_lib(
    name = "ara",
    model_src = ["//platform/common/someip_demon/model:instance"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "someip_demon",
    srcs = [
        "main.cc",
        "someip_demon.cc",
        "someip_demon.h",
    ],
    visibility = ["//platform/common/someip_demon:__subpackages__"],
    deps = [
        ":ara",
        "@srp_platform//ara/exec:adaptive_application_lib",
        "//core/common:condition_lib",
        "//core/json:simba_json",
        "//platform/common/someip_demon/code/common/com/impl:multicast_controller",
        "//platform/common/someip_demon/code/sd:sd_controller",
        "//platform/common/someip_demon/code/common:controller",
        "//platform/common/someip_demon/code/connector",
        "//platform/common/someip_demon/code/db",
    ],
)
