load("@rules_python//python:defs.bzl", "py_binary")

py_binary(
    name = "test",
    srcs = [
        "addition_method_parameters.py",
        "temperature_msg.py",
        "test.py",
    ],
    imports = ["."],
    deps = [
        "@srp_platform//third_party/python:someip_lib",
    ],
)

py_binary(
    name = "test2",
    srcs = [
        "addition_method_parameters.py",
        "temperature_msg.py",
        "test2.py",
    ],
    imports = ["."],
    deps = [
        "@srp_platform//third_party/python/someipy/src:someipy_lib",
    ],
)

py_binary(
    name = "test3",
    srcs = [
        "addition_method_parameters.py",
        "temperature_msg.py",
        "test3.py",
    ],
    imports = ["."],
    deps = [
        "@srp_platform//third_party/python:someip_lib",
    ],
)

py_binary(
    name = "offer_event",
    srcs = [
        "addition_method_parameters.py",
        "offer_event.py",
        "temperature_msg.py",
    ],
    imports = ["."],
    deps = [
        "@srp_platform//third_party/python:someip_lib",
    ],
)
