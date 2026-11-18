load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")
load("@srp_platform//tools/common:cpu_def.bzl", "cpu_def")
load("@srp_platform//tools/common:flash_ecu.bzl", "flash_ecu")
load("@srp_platform//tools/model_generator/ara:adaptive.bzl", "ara_json2sm_config")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application")
load("@srp_platform//tools/model_generator/ara/mavlink:mavlink.bzl", "ara_mavlink_dialect")

# A ready CPU package for hardware tests of the MAVLink stack over the `example_udp_radio` driver: the
# station (PC) is on the same Ethernet as the RPi Zero 2 W (or the BBB), no radio hardware needed.
# Same applications as `//test_cpu/cpu/ec:pkg` (`on: mavlink`), so the station dialect is
# `//test_cpu/cpu/ec:dialect`. Build `:pkg`, flash, run the ground tools. Instructions and the expected
# values: tests/mavlink/udp_cpu/README.md.

exports_files([
    "logger_config.json",
    "mavlink_daemon_config.json",
    "platform_config.json",
    "udp_ground.json",
    "udp_ground_b.json",
])

# The daemon "as in srp": `example_udp_radio` linked in statically, run under Execution Management.
mavlink_daemon_binary(
    name = "udp_daemon",
    drivers = ["//ara/com/mavlink/examples/udp_radio:example_udp_radio"],
)

# The target name is the application name of the model (install path /srp/opt/mavlink_daemon, the
# name the EC package uses), the config and the driver files go to its `etc` directory.
adaptive_application(
    name = "mavlink_daemon",
    bin = ":udp_daemon",
    etcs = [
        "mavlink_daemon_config.json",
        "udp_ground.json",
        "udp_ground_b.json",
    ],
    model_src = ["@srp_platform//platform/common/mavlink_daemon/model:instance"],
)

ara_json2sm_config(
    name = "sm_config",
    src = "//platform_definition/sm:example_config",
)

filegroup(
    name = "cpu_config",
    srcs = ["platform_config.json"],
)

cpu_def(
    name = "pkg",
    config = ":cpu_config",
    etcs = [
        "diag_config.json",
        "logger_config.json",
    ],
    platform_etcs = [
        ":sm_config",
        "machine_states.json",
        "platform_config.json",
    ],
    srp_components = [
        ":mavlink_daemon",
        "//example_app/models/ExampleApp",
        "//example_app2/models/ExampleApp2",
        "//platform/common/diag_demon/model:diag_demon",
        "//platform/common/em/model:em",
        "//platform/common/logger/model:logger",
        "//platform/common/someip_demon/model:someip_demon",
        "//platform/common/state_manager/model:state_manager",
    ],
)

# bazel run //tests/mavlink/udp_cpu:flash   (RPi Zero 2 W over USB gadget: 192.168.7.2; WiFi/Ethernet
# 192.168.10.51 and QEMU rpi 192.168.10.101: see the README)
flash_ecu(
    name = "flash",
    file = [":pkg"],
    ip = "192.168.7.2",
    user_name = "root",
)

# The same models with the config of this package: the generator checks the config against the models
# (links, `mtu`, the bandwidth budget), so a wrong edit of the config fails the build. The output is the
# same dialect as `//test_cpu/cpu/ec:dialect` (the models and the system/component ids are the same).
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
)

# The soak test (README, "Soak test"): the CPU under CPU load for hours. `soak_cpu` runs on the CPU
# (busybox sh), `soak_station` and `soak_verdict` on the PC, `soak_run` drives them over ssh;
# `soak_run --local` does it all on this host (`soak_local` is the CPU there).
py_library(
    name = "soak_station_lib",
    srcs = ["soak/soak_station.py"],
    imports = ["soak"],
    deps = [
        "//third_party/python:pymavlink",
        "//tools/mavlink:monitor_lib",
    ],
)

py_binary(
    name = "soak_station",
    srcs = ["soak/soak_station.py"],
    data = [
        ":mavlink_daemon_config.json",
        ":dialect",
    ],
    imports = ["soak"],
    main = "soak/soak_station.py",
    deps = [":soak_station_lib"],
)

py_binary(
    name = "soak_verdict",
    srcs = ["soak/soak_verdict.py"],
    imports = ["soak"],
    main = "soak/soak_verdict.py",
    deps = [":soak_station_lib"],
)

py_binary(
    name = "soak_local",
    testonly = True,
    srcs = ["soak/soak_local.py"],
    data = [
        ":mavlink_daemon_config.json",
        "//tests/mavlink:example_app2_no_em",
        "//tests/mavlink:example_app_no_em",
        "//tests/mavlink:example_udp_daemon_host",
    ],
    imports = ["soak"],
    main = "soak/soak_local.py",
    deps = [":soak_station_lib"],
)

sh_binary(
    name = "soak_cpu",
    srcs = ["soak/soak_cpu.sh"],
)

sh_binary(
    name = "soak_run",
    testonly = True,
    srcs = ["soak/soak_run.sh"],
    data = [
        ":soak_cpu",
        ":soak_local",
        ":soak_station",
        ":soak_verdict",
    ],
)

py_test(
    name = "soak_station_test",
    srcs = ["soak/soak_station_test.py"],
    data = [":dialect"],
    env = {"EXAMPLE_DIALECT": "$(locations :dialect)"},
    imports = ["soak"],
    main = "soak/soak_station_test.py",
    deps = [
        ":soak_station_lib",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# The whole soak on this host, with shortened phases (60 s): the daemon + the example applications,
# the station, the hogs and the verdict.
py_test(
    name = "soak_run_test",
    size = "large",
    srcs = ["soak/soak_run_test.py"],
    data = [":soak_run"],
    env = {"SOAK_RUN": "$(rootpath :soak_run)"},
    imports = ["soak"],
    main = "soak/soak_run_test.py",
    tags = ["exclusive"],
    deps = ["//third_party/python:pytest"],
)
