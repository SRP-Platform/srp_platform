load("@srp_platform//tools/common:cpu_def.bzl", "cpu_def")
load("@srp_platform//tools/model_generator/ara:adaptive.bzl", "ara_json2sm_config")
load("@srp_platform//tools/common:flash_ecu.bzl", "flash_ecu")

flash_ecu(
    name = "flash",
    file = ["//test_cpu/cpu/fc:pkg"],
    ip = "192.168.10.102",
    user_name = "root",
)
ara_json2sm_config(
    name = "sm_config",
    src = "//platform_definition/sm:example_config",
)

cpu_def(
    name = "pkg",
    config = ":config",
    etcs = [
        "diag_config.json",
        "logger_config.json",
    ],
    platform_etcs = [
        "//test_cpu/cpu/fc:sm_config",
        "platform_config.json",
    ],
    srp_components = [
        "//platform/common/diag_demon/model:diag_demon",
        "//platform/common/em/model:em",
        "//platform/common/logger/model:logger",
        "//platform/common/someip_demon/model:someip_demon",
        "//platform/common/state_manager/model:state_manager",
        # "//deployment/example_adaptive/ExampleApp2",
    ],
)

filegroup(
    name = "config",
    srcs = ["platform_config.json"],
    visibility = ["//deployment/cpu:__subpackages__"],
)
