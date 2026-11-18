load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "mav_helpers",
    srcs = ["mav_helpers.py"],
    imports = ["."],
    deps = ["//third_party/python:pymavlink"],
)

py_library(
    name = "channel_sim_lib",
    srcs = ["channel_sim.py"],
    imports = ["."],
)

py_test(
    name = "channel_sim_test",
    srcs = ["channel_sim_test.py"],
    main = "channel_sim_test.py",
    deps = [
        ":channel_sim_lib",
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

py_test(
    name = "mav_helpers_test",
    srcs = ["mav_helpers_test.py"],
    main = "mav_helpers_test.py",
    deps = [
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

py_library(
    name = "tshark_harness",
    srcs = ["tshark_harness.py"],
    imports = ["."],
)

py_test(
    name = "tshark_lua_test",
    srcs = ["tshark_lua_test.py"],
    main = "tshark_lua_test.py",
    deps = [
        ":mav_helpers",
        ":tshark_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)
py_library(
    name = "fuzz_corpus_lib",
    srcs = ["fuzz_corpus.py"],
    imports = ["."],
    deps = [
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

py_binary(
    name = "fuzz_corpus",
    srcs = ["fuzz_corpus.py"],
    main = "fuzz_corpus.py",
    deps = [":fuzz_corpus_lib"],
)

py_test(
    name = "fuzz_corpus_test",
    srcs = ["fuzz_corpus_test.py"],
    main = "fuzz_corpus_test.py",
    deps = [
        ":fuzz_corpus_lib",
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# --- E2E na PRAWDZIWYM runtime: MavlinkDaemon (daemon_host) + aplikacja (peer C++ z modelu peer_model/) ---
py_library(
    name = "peer_lib",
    srcs = ["peer_api.py"],
    imports = ["."],
    deps = [
        ":channel_sim_lib",
        ":mav_helpers",
        "//third_party/python:pymavlink",
    ],
)

PEER_DATA = [
    ":daemon_host",
    ":mavlink_peer",
    "//tests/mavlink/peer_model_v1:mavlink_peer_v1",
    "//tests/mavlink/collide_model:mavlink_peer_collide",
]

PEER_ENV = {
    "MAVLINK_DAEMON_HOST": "$(rootpath :daemon_host)",
    "MAVLINK_PEER": "$(rootpath :mavlink_peer)",
    "MAVLINK_PEER_V1": "$(rootpath //tests/mavlink/peer_model_v1:mavlink_peer_v1)",
    "MAVLINK_PEER_COLLIDE": "$(rootpath //tests/mavlink/collide_model:mavlink_peer_collide)",
}

[py_test(
    name = name,
    srcs = [name + ".py"],
    main = name + ".py",
    data = PEER_DATA,
    env = PEER_ENV,
    deps = [
        ":fuzz_corpus_lib",
        ":mav_helpers",
        ":peer_lib",
        ":tshark_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
) for name in [
    "e2e_interop_test",
    "e2e_app_lifecycle_test",
    "e2e_channel_test",
    "e2e_fuzz_test",
    "e2e_soak_test",
    "e2e_signing_persist_test",
]]

# soak 30 min - reczny (poza domyslnym `bazel test //...`)
py_test(
    name = "e2e_soak_30min",
    srcs = ["e2e_soak_test.py"],
    main = "e2e_soak_test.py",
    data = PEER_DATA,
    env = dict(PEER_ENV, MAVLINK_SOAK_SECONDS = "1800"),
    size = "enormous",
    tags = ["manual"],
    deps = [
        ":peer_lib",
        "//third_party/python:pytest",
    ],
)

# fuzz losowy 10 min - reczny
py_test(
    name = "e2e_fuzz_10min",
    srcs = ["e2e_fuzz_test.py"],
    main = "e2e_fuzz_test.py",
    data = PEER_DATA,
    env = dict(PEER_ENV, MAVLINK_FUZZ_SECONDS = "600"),
    size = "enormous",
    tags = ["manual"],
    deps = [
        ":fuzz_corpus_lib",
        ":mav_helpers",
        ":peer_lib",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# --- peer C++ na prawdziwym runtime (model z peer_model/ przez generator) ---
ara_runtime_lib(
    name = "peer_runtime",
    model_src = [
        "peer_model/app_config.json",
        "peer_model/service_Example.json",
        "peer_model/type_Info.json",
        "peer_model/type_ModeIn.json",
        "peer_model/type_ModeOut.json",
        "peer_model/type_NameData.json",
        "peer_model/type_StatusData.json",
    ],
)

cc_binary(
    name = "mavlink_peer",
    srcs = ["peer/mavlink_peer.cc"],
    deps = [
        ":peer_runtime",
        "@com_json//:json",
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com/mavlink/client:daemon_client",
    ],
)

# Ten sam rdzen co produkcyjny `mavlink_daemon`, ale bez Execution Management (na PC go nie ma) i ze
# sterowaniem JSON lines (liczniki laczy, zatrzymanie z zapisem stanu podpisu).
cc_binary(
    name = "daemon_host",
    testonly = True,
    srcs = ["peer/daemon_host.cc"],
    deps = [
        "@com_json//:json",
        "@srp_platform//platform/common/mavlink_daemon/code:daemon_config",
        "@srp_platform//platform/common/mavlink_daemon/code:mavlink_daemon_core",
    ],
)

# --- E2E przykladow: example_app (provide) + example_app2 (require) na jednym daemonie CPU test_cpu/cpu/ec ---
py_test(
    name = "e2e_examples_test",
    srcs = ["e2e_examples_test.py"],
    main = "e2e_examples_test.py",
    data = [
        ":daemon_host",
        "//example_app/ExampleApp/code/application:ExampleApp",
        "//example_app2/ExampleApp2/code/application:ExampleApp",
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:mavlink_daemon_config.json",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath //example_app/ExampleApp/code/application:ExampleApp)",
        "EXAMPLE_APP2": "$(rootpath //example_app2/ExampleApp2/code/application:ExampleApp)",
        "MAVLINK_DAEMON_HOST": "$(rootpath :daemon_host)",
        "EXAMPLE_DAEMON_CONFIG": "$(rootpath //test_cpu/cpu/ec:mavlink_daemon_config.json)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
    },
    deps = [
        ":channel_sim_lib",
        ":mav_helpers",
        ":peer_lib",
        ":tshark_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

# --- E2E MavlinkDaemona: prawdziwy proces daemona + pty + pymavlink + klient IPC w Pythonie ---
py_library(
    name = "daemon_client",
    srcs = ["daemon_client.py"],
    imports = ["."],
)

py_test(
    name = "e2e_daemon_test",
    srcs = ["e2e_daemon_test.py"],
    main = "e2e_daemon_test.py",
    data = [
        "//ara/com/mavlink:libtest_radio_plugin_ctl.so",
        ":daemon_host",
    ],
    env = {
        "CTL_PLUGIN": "$(rootpath //ara/com/mavlink:libtest_radio_plugin_ctl.so)",
        "DAEMON_HOST": "$(rootpath :daemon_host)",
    },
    deps = [
        ":daemon_client",
        ":mav_helpers",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)
