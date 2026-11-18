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

# --- E2E na PRAWDZIWYM runtime (peer C++ z modelu peer_model/ przez generator) ---
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
    ":mavlink_peer",
    "//tests/mavlink/peer_model_v1:mavlink_peer_v1",
]

PEER_ENV = {
    "MAVLINK_PEER": "$(rootpath :mavlink_peer)",
    "MAVLINK_PEER_V1": "$(rootpath //tests/mavlink/peer_model_v1:mavlink_peer_v1)",
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
        "@srp_platform//ara/com/mavlink:link_manager",
        "@srp_platform//ara/com/mavlink:link_transport_registry",
        "@srp_platform//ara/com/mavlink:serial_transport",
    ],
)

exports_files(["peer/mavlink_peer.cc"])  # ten sam kod peera buduje wariant v1 w pakiecie peer_model_v1

# --- E2E przykladow example_app (skeleton) <-> example_app2 (proxy) przez channel_sim ---
py_test(
    name = "m3_examples_e2e_test",
    srcs = ["m3_examples_e2e_test.py"],
    main = "m3_examples_e2e_test.py",
    data = [
        "//example_app/ExampleApp/code/application:ExampleApp",
        "//example_app2/ExampleApp2/code/application:ExampleApp",
    ],
    env = {
        "EXAMPLE_APP1": "$(rootpath //example_app/ExampleApp/code/application:ExampleApp)",
        "EXAMPLE_APP2": "$(rootpath //example_app2/ExampleApp2/code/application:ExampleApp)",
    },
    deps = [
        ":channel_sim_lib",
        "//third_party/python:pytest",
    ],
)
