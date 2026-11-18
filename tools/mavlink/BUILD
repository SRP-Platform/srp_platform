load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

# Narzedzia MAVLink dla uzytkownika: keygen (klucz podpisu) i serial2udp (strumien z UART do Wiresharka).
package(default_visibility = ["//visibility:public"])

py_library(
    name = "keygen_lib",
    srcs = ["keygen.py"],
    imports = ["."],
)

py_library(
    name = "serial2udp_lib",
    srcs = ["serial2udp.py"],
    imports = ["."],
)

py_binary(
    name = "keygen",
    srcs = ["keygen.py"],
    main = "keygen.py",
)

py_binary(
    name = "serial2udp",
    srcs = ["serial2udp.py"],
    main = "serial2udp.py",
    deps = ["//third_party/python:pymavlink"],  # --dialect: walidacja CRC (mavcrc)
)

py_test(
    name = "keygen_test",
    srcs = ["keygen_test.py"],
    main = "keygen_test.py",
    deps = [
        ":keygen_lib",
        "//third_party/python:pytest",
    ],
)

py_test(
    name = "serial2udp_test",
    srcs = ["serial2udp_test.py"],
    main = "serial2udp_test.py",
    data = [":serial2udp"],
    env = {"SERIAL2UDP": "$(rootpath :serial2udp)"},
    deps = [
        ":serial2udp_lib",
        "//tests/mavlink:mav_helpers",
        "//tests/mavlink:tshark_harness",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)
py_test(
    name = "readme_models_test",
    srcs = ["readme_models_test.py"],
    main = "readme_models_test.py",
    data = [
        "//docs/mavlink:README.md",
        "//tools/model_generator/ara/mavlink:mavlink_gen",
    ],
    env = {
        "MAVLINK_README": "$(rootpath //docs/mavlink:README.md)",
        "MAVLINK_GEN": "$(rootpath //tools/model_generator/ara/mavlink:mavlink_gen)",
    },
    deps = [
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)
