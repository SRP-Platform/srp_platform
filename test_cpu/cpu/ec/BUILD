load("@srp_platform//tools/common:cpu_def.bzl", "cpu_def")
load("@srp_platform//tools/common:flash_ecu.bzl", "flash_ecu")
load("@srp_platform//tools/model_generator/ara:adaptive.bzl", "ara_json2sm_config")
load("@srp_platform//tools/model_generator/ara/mavlink:mavlink.bzl", "ara_mavlink_dialect", "ara_mavlink_system")

exports_files([
    "mavlink_daemon_config.json",
    "mavlink_daemon_config_cpu2.json",
])

flash_ecu(
    name = "flash",
    file = ["//test_cpu/cpu/ec:pkg"],
    ip = "192.168.10.101",
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
        "mavlink_daemon_config.json",
    ],
    platform_etcs = [
        "//test_cpu/cpu/ec:sm_config",
        "machine_states.json",
        "platform_config.json",
    ],
    srp_components = [
        "//platform/common/diag_demon/model:diag_demon",
        "//platform/common/em/model:em",
        "//platform/common/logger/model:logger",
        "//platform/common/mavlink_daemon/model:mavlink_daemon",
        "//platform/common/someip_demon/model:someip_demon",
        "//platform/common/state_manager/model:state_manager",
        "//example_app/models/ExampleApp",
        "//example_app2/models/ExampleApp2",
    ],
)

filegroup(
    name = "config",
    srcs = ["platform_config.json"],
    visibility = ["//deployment/cpu:__subpackages__"],
)

# MAVLink dialect of the CPU: the models of ALL applications with `on: mavlink` plus the daemon config.
# Outputs: srp_example.{xml,lua,py} and srp_example_links.md (the element x link matrix).
ara_mavlink_dialect(
    name = "dialect",
    apps = [
        "//example_app/models/ExampleApp:example_app.json",
        "//example_app2/models/ExampleApp2:example_app.json",
    ],
    daemon_config = "mavlink_daemon_config.json",
    data = [
        "//example_app/models/ExampleApp:service_instance",
    ],
    dialect = "srp_example",
    visibility = [
        "//tests/mavlink:__pkg__",
        "//tools/mavlink:__pkg__",
    ],
)

# A second CPU of the same rocket for the multi-CPU example and test: the same applications (the test
# runs two daemons on one machine) and the daemon config with component_id 2.
ara_mavlink_dialect(
    name = "dialect_cpu2",
    apps = [
        "//example_app/models/ExampleApp:example_app.json",
        "//example_app2/models/ExampleApp2:example_app.json",
    ],
    daemon_config = "mavlink_daemon_config_cpu2.json",
    data = [
        "//example_app/models/ExampleApp:service_instance",
    ],
    dialect = "srp_example_cpu2",
)

# ONE dialect for the station from the dialects of both CPUs (docs/mavlink/README.md, "Several CPUs").
# Outputs: srp_example_system.{xml,lua,py} and srp_example_system_links.md.
ara_mavlink_system(
    name = "system",
    cpus = [
        ":dialect",
        ":dialect_cpu2",
    ],
    daemon_configs = [
        "mavlink_daemon_config.json",
        "mavlink_daemon_config_cpu2.json",
    ],
    dialect = "srp_example_system",
    visibility = ["//tests/mavlink:__pkg__", "//tools/mavlink:__pkg__"],
)
