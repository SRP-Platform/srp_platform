load("@srp_platform//tools/common:cpu_def.bzl", "cpu_def")
load("@srp_platform//tools/common:flash_ecu.bzl", "flash_ecu")
load("@srp_platform//tools/model_generator/ara:adaptive.bzl", "ara_json2sm_config")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "adaptive_application")

# A ready CPU package for the end-to-end test of MavlinkLinkService on the real `ipc` (QEMU rpi or a
# board): Execution Management, someip_demon, state_manager, logger, diag_demon, the MavlinkDaemon with the test
# radio drivers (`//tests/mavlink:link_stats_daemon`, config = `//tests/mavlink:link_stats_daemon_config.json`)
# and the client `link_stats_app`. Nothing to edit: build `:pkg`, flash, read the log. Instructions and the
# expected lines: tests/mavlink/README.md, "LinkStats on the real ipc".

# The daemon reads `mavlink_daemon_config.json` from its etc directory; the test config has another name.
genrule(
    name = "daemon_config",
    srcs = ["//tests/mavlink:link_stats_daemon_config.json"],
    outs = ["mavlink_daemon_config.json"],
    cmd = "cp $< $@",
)

# The target name is the application name of the model (install path /srp/opt/mavlink_daemon, bin_path in srp_app.json).
adaptive_application(
    name = "mavlink_daemon",
    bin = "//tests/mavlink:link_stats_daemon",
    etcs = [":daemon_config"],
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
        "//platform/common/diag_demon/model:diag_demon",
        "//platform/common/em/model:em",
        "//platform/common/logger/model:logger",
        "//platform/common/someip_demon/model:someip_demon",
        "//platform/common/state_manager/model:state_manager",
        "//tests/mavlink/link_stats_app",
    ],
)

# bazel run //tests/mavlink/link_stats_cpu:flash   (QEMU rpi: 192.168.10.101, see the README)
flash_ecu(
    name = "flash",
    file = [":pkg"],
    ip = "192.168.10.101",
    user_name = "root",
)
