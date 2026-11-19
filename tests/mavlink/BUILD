load("@rules_python//python:defs.bzl", "py_library", "py_test")

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
cc_binary(
    name = "daemon_host",
    testonly = True,
    srcs = ["daemon_host.cc"],
    deps = [
        "@com_json//:json",
        "@srp_platform//platform/common/mavlink_daemon/code:daemon_config",
        "@srp_platform//platform/common/mavlink_daemon/code:mavlink_daemon_core",
    ],
)

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
