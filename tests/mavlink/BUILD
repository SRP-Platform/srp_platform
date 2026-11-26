load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@srp_platform//platform/common/mavlink_daemon:defs.bzl", "mavlink_daemon_binary")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "mav_helpers",
    srcs = ["mav_helpers.py"],
    imports = ["."],
)

py_library(
    name = "channel_sim_lib",
    srcs = ["channel_sim.py"],
    imports = ["."],
)

py_library(
    name = "tshark_harness",
    srcs = ["tshark_harness.py"],
    imports = ["."],
)

py_library(
    name = "harness",
    srcs = ["harness.py"],
    imports = ["."],
    deps = [
        ":channel_sim_lib",
        ":mav_helpers",
        "//third_party/python:pymavlink",
    ],
)

# The IPC client of the MavlinkDaemon in Python; the E220 end-to-end test in srp uses it.
py_library(
    name = "daemon_client",
    srcs = ["daemon_client.py"],
    imports = ["."],
)

# --- self-tests of the harness ---
py_test(
    name = "channel_sim_test",
    srcs = ["channel_sim_test.py"],
    main = "channel_sim_test.py",
    deps = [
        ":channel_sim_lib",
        "//third_party/python:pytest",
    ],
)

py_test(
    name = "mav_helpers_test",
    srcs = ["mav_helpers_test.py"],
    main = "mav_helpers_test.py",
    deps = [
        ":mav_helpers",
        "//third_party/python:pytest",
    ],
)

# tshark and the generated Lua dissector; a missing tshark fails the test.
py_test(
    name = "tshark_lua_test",
    srcs = ["tshark_lua_test.py"],
    data = ["//test_cpu/cpu/ec:dialect"],
    env = {"EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)"},
    main = "tshark_lua_test.py",
    deps = [
        ":harness",
        ":mav_helpers",
        ":tshark_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# --- end-to-end tests on the example applications ---

# The same core as the production `mavlink_daemon` without Execution Management (the host has
# none), controlled through JSON lines.
mavlink_daemon_binary(
    name = "daemon_host",
    testonly = True,
    host = True,
)

# A daemon binary "as in srp": the user writes only this BUILD entry. The driver is registered
# statically by the test driver library (ids `test_serial_a`, `test_serial_b`).
mavlink_daemon_binary(
    name = "static_driver_daemon_host",
    testonly = True,
    drivers = ["//ara/com/mavlink:test_radio_driver"],
    host = True,
)

# Two libraries register the same driver id: the daemon must refuse to start and name the id.
mavlink_daemon_binary(
    name = "duplicate_driver_daemon_host",
    testonly = True,
    drivers = [
        "//ara/com/mavlink:test_radio_driver",
        "//ara/com/mavlink:test_radio_driver_duplicate",
    ],
    host = True,
)

# The daemon for the end-to-end test of MavlinkLinkService on the real ipc (QEMU rpi, instructions:
# tests/mavlink/README.md, "LinkStats on the real ipc"): the platform application under Execution
# Management (not testonly: it goes into the package //tests/mavlink/link_stats_cpu:pkg; no production
# package depends on it), links on MemoryRadio drivers (`test_stats_a` reports nothing, `test_stats_b` pushes
# rssi -71 / noise -110).
mavlink_daemon_binary(
    name = "link_stats_daemon",
    drivers = ["//ara/com/mavlink:test_stats_radio_driver"],
)

exports_files(["link_stats_daemon_config.json"])

# The example applications with a main that skips Execution Management.
cc_binary(
    name = "example_app_no_em",
    testonly = True,
    srcs = ["no_em_main_app1.cc"],
    deps = [
        "//core/common:condition_lib",
        "//example_app/ExampleApp/code/application:example_app_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
    ],
)

cc_binary(
    name = "example_app2_no_em",
    testonly = True,
    srcs = ["no_em_main_app2.cc"],
    deps = [
        "//example_app2/ExampleApp2/code/application:example_app_lib",
        "@srp_platform//ara/exec:adaptive_application_lib",
    ],
)

py_test(
    name = "e2e_examples_test",
    size = "large",
    srcs = ["e2e_examples_test.py"],
    data = [
        ":daemon_host",
        ":example_app2_no_em",
        ":example_app_no_em",
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:mavlink_daemon_config.json",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "EXAMPLE_APP2": "$(rootpath :example_app2_no_em)",
        "MAVLINK_DAEMON_HOST": "$(rootpath :daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config.json)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
    },
    main = "e2e_examples_test.py",
    deps = [
        ":daemon_client",
        ":harness",
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# The same scenario on a daemon binary produced by `mavlink_daemon_binary` with a test driver
# linked in statically (a link on `test_serial_b`), and the duplicate-id start error.
py_test(
    name = "e2e_static_driver_test",
    size = "large",
    srcs = [
        "e2e_examples_test.py",
        "e2e_static_driver_test.py",
    ],
    data = [
        ":duplicate_driver_daemon_host",
        ":example_app2_no_em",
        ":example_app_no_em",
        ":static_driver_daemon_host",
        ":static_driver_daemon_config.json",
        "//test_cpu/cpu/ec:dialect",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "EXAMPLE_APP2": "$(rootpath :example_app2_no_em)",
        "MAVLINK_DAEMON_HOST": "$(rootpath :static_driver_daemon_host)",
        "DUPLICATE_DRIVER_DAEMON_HOST": "$(rootpath :duplicate_driver_daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath :static_driver_daemon_config.json)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
    },
    main = "e2e_static_driver_test.py",
    deps = [
        ":daemon_client",
        ":harness",
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# Two MavlinkDaemons ("two CPUs") on one machine with their own sockets and ptys and one station
# dialect from `ara_mavlink_system`.
py_test(
    name = "e2e_two_cpus_test",
    size = "large",
    srcs = ["e2e_two_cpus_test.py"],
    data = [
        ":daemon_host",
        ":example_app_no_em",
        "//test_cpu/cpu/ec:mavlink_daemon_config.json",
        "//test_cpu/cpu/ec:mavlink_daemon_config_cpu2.json",
        "//test_cpu/cpu/ec:system",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "MAVLINK_DAEMON_HOST": "$(rootpath :daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config.json)",
        "EXAMPLE_DAEMON_CONFIG_CPU2": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config_cpu2.json)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:system)",
    },
    main = "e2e_two_cpus_test.py",
    deps = [
        ":harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# --- the example radio driver over UDP (ara/com/mavlink/examples/udp_radio) ---

# A daemon "as in srp" with `example_udp_radio` linked in statically, without Execution Management.
mavlink_daemon_binary(
    name = "example_udp_daemon_host",
    testonly = True,
    drivers = ["//ara/com/mavlink/examples/udp_radio:example_udp_radio"],
    host = True,
)

# The ground station on UDP and the stack (daemon + example applications) of the UDP tests.
py_library(
    name = "udp_harness",
    srcs = ["udp_harness.py"],
    imports = ["."],
    deps = [
        ":harness",
        ":mav_helpers",
        "//third_party/python:pymavlink",
    ],
)

py_test(
    name = "e2e_udp_radio_test",
    size = "large",
    srcs = ["e2e_udp_radio_test.py"],
    data = [
        ":example_app2_no_em",
        ":example_app_no_em",
        ":example_udp_daemon_host",
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:mavlink_daemon_config.json",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "EXAMPLE_APP2": "$(rootpath :example_app2_no_em)",
        "UDP_DAEMON_HOST": "$(rootpath :example_udp_daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config.json)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
    },
    main = "e2e_udp_radio_test.py",
    deps = [
        ":harness",
        ":udp_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# Conformance of the wire traffic to the model: the traffic of the daemon on UDP is decoded ONLY by
# the pymavlink module generated for the CPU dialect and compared with what the models allow
# (elements, rates, values, seq, methods, signing, bandwidth budget).
py_test(
    name = "udp_conformance_test",
    size = "large",
    srcs = ["udp_conformance_test.py"],
    data = [
        ":example_app2_no_em",
        ":example_app_no_em",
        ":example_udp_daemon_host",
        "//example_app/models/ExampleApp:example_app.json",
        "//example_app/models/ExampleApp:service_instance",
        "//example_app2/models/ExampleApp2:example_app.json",
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:mavlink_daemon_config.json",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "EXAMPLE_APP2": "$(rootpath :example_app2_no_em)",
        "UDP_DAEMON_HOST": "$(rootpath :example_udp_daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config.json)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
        "MODEL_APP1": "$(rootpath //example_app/models/ExampleApp:example_app.json)",
        "MODEL_APP2": "$(rootpath //example_app2/models/ExampleApp2:example_app.json)",
        "MODEL_SERVICES": "$(rootpaths //example_app/models/ExampleApp:service_instance)",
    },
    main = "udp_conformance_test.py",
    deps = [
        ":harness",
        ":mav_helpers",
        ":udp_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# The generated ground monitor (`<dialect>_monitor.py`, runtime tools/mavlink/mavlink_monitor.py) on
# the example CPU: the same scenario on UDP (both links) and on a pty as a UART, a tap copies the
# traffic of a link to the monitor.
py_test(
    name = "e2e_monitor_test",
    size = "large",
    srcs = [
        "e2e_examples_test.py",
        "e2e_monitor_test.py",
    ],
    data = [
        ":daemon_host",
        ":example_app2_no_em",
        ":example_app_no_em",
        ":example_udp_daemon_host",
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:dialect_monitor",
        "//test_cpu/cpu/ec:mavlink_daemon_config.json",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "EXAMPLE_APP2": "$(rootpath :example_app2_no_em)",
        "MAVLINK_DAEMON_HOST": "$(rootpath :daemon_host)",
        "UDP_DAEMON_HOST": "$(rootpath :example_udp_daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config.json)",
        "MONITOR": "$(rootpath //test_cpu/cpu/ec:dialect_monitor)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
    },
    main = "e2e_monitor_test.py",
    deps = [
        ":daemon_client",
        ":harness",
        ":mav_helpers",
        ":udp_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
        "//tools/mavlink:monitor_lib",
    ],
)

# The config and the driver files of `//tests/mavlink/udp_cpu` (the package for hardware tests): the
# links match the models, `example_udp_radio` is linked in, the station address is the documented one,
# and the stack with that config gives the expected monitor result on UDP.
py_test(
    name = "udp_cpu_test",
    size = "large",
    srcs = [
        "e2e_examples_test.py",
        "e2e_monitor_test.py",
        "udp_cpu_station.py",
        "udp_cpu_test.py",
    ],
    data = [
        ":example_app2_no_em",
        ":example_app_no_em",
        ":daemon_host",
        ":example_udp_daemon_host",
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:dialect_monitor",
        "//tests/mavlink/udp_cpu:logger_config.json",
        "//tests/mavlink/udp_cpu:mavlink_daemon_config.json",
        "//tests/mavlink/udp_cpu:platform_config.json",
        "//tests/mavlink/udp_cpu:udp_ground.json",
        "//tests/mavlink/udp_cpu:udp_ground_b.json",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath :example_app_no_em)",
        "EXAMPLE_APP2": "$(rootpath :example_app2_no_em)",
        "MAVLINK_DAEMON_HOST": "$(rootpath :daemon_host)",
        "UDP_DAEMON_HOST": "$(rootpath :example_udp_daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //tests/mavlink/udp_cpu:mavlink_daemon_config.json)",
        "LOGGER_CONFIG": "$(rootpath //tests/mavlink/udp_cpu:logger_config.json)",
        "PLATFORM_CONFIG": "$(rootpath //tests/mavlink/udp_cpu:platform_config.json)",
        "UDP_GROUND": "$(rootpath //tests/mavlink/udp_cpu:udp_ground.json)",
        "UDP_GROUND_B": "$(rootpath //tests/mavlink/udp_cpu:udp_ground_b.json)",
        "MONITOR": "$(rootpath //test_cpu/cpu/ec:dialect_monitor)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
    },
    main = "udp_cpu_test.py",
    deps = [
        ":daemon_client",
        ":harness",
        ":mav_helpers",
        ":udp_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
        "//tools/mavlink:monitor_lib",
    ],
)

# The PC side of the hardware test of `//tests/mavlink/udp_cpu`: listens for the events of the CPU and
# sends SetMode/DebugDump requests to it (README of that package).
py_binary(
    name = "udp_cpu_station",
    srcs = ["udp_cpu_station.py"],
    args = ["--dialect-py", "$(rootpath //test_cpu/cpu/ec:dialect_dialect_py)"],
    data = ["//test_cpu/cpu/ec:dialect_dialect_py"],
    main = "udp_cpu_station.py",
    deps = ["//third_party/python:pymavlink"],
)
