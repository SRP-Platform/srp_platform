load("@srp_platform//tools/common:cpu_def.bzl", "cpu_def")
load("@srp_platform//tools/model_generator/platform:adaptive.bzl", "platform_json2sm_config")
load("@srp_platform//tools/common:flash_ecu.bzl", "flash_ecu")

flash_ecu(
    name = "flash",
    file = ["//test_cpu/cpu/ec:pkg"],
    ip = "192.168.10.101",
    user_name = "root",
)

platform_json2sm_config(
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
        "//test_cpu/cpu/ec:sm_config",
        "platform_config.json",
    ],
    srp_components = [
        "//apps/common/diag_demon/model:diag_demon",
        "//apps/common/em/model:em",
        "//apps/common/logger/model:logger",
        "//apps/common/ipc_service_discover/model:ipc_service_discover",
        "//example/model:example_app",
        # "//platform/common/someip_demon/model:someip_demon",
        "//apps/common/state_manager/model:state_manager",
        # "//deployment/example_adaptive/ExampleApp2",
    ],
)

filegroup(
    name = "config",
    srcs = ["platform_config.json"],
    visibility = ["//deployment/cpu:__subpackages__"],
)
