load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

# User tools: keygen (signing key), serial2udp (a UART stream for live Wireshark) and the runtime of
# the generated ground monitor (<dialect>_monitor.py).
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

# The runtime of `<dialect>_monitor.py` (ara_mavlink_dialect / ara_mavlink_system output).
py_library(
    name = "monitor_lib",
    srcs = ["mavlink_monitor.py"],
    imports = ["."],
    deps = [":serial2udp_lib"],
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
    deps = ["//third_party/python:pymavlink"],
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
    data = ["//test_cpu/cpu/ec:dialect"],
    env = {"EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)"},
    main = "serial2udp_test.py",
    deps = [
        ":serial2udp_lib",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)

py_test(
    name = "mavlink_monitor_test",
    srcs = ["mavlink_monitor_test.py"],
    data = [
        "//test_cpu/cpu/ec:dialect",
        "//test_cpu/cpu/ec:dialect_monitor",
        "//test_cpu/cpu/ec:system",
        "//test_cpu/cpu/ec:system_monitor",
    ],
    env = {
        "MONITOR_DIALECT": "$(rootpath //test_cpu/cpu/ec:dialect_monitor)",
        "MONITOR_SYSTEM": "$(rootpath //test_cpu/cpu/ec:system_monitor)",
        "EXAMPLE_DIALECT": "$(locations //test_cpu/cpu/ec:dialect)",
        "EXAMPLE_SYSTEM": "$(locations //test_cpu/cpu/ec:system)",
    },
    main = "mavlink_monitor_test.py",
    deps = [
        ":monitor_lib",
        "//third_party/python:pymavlink",
        "//third_party/python:pytest",
    ],
)
