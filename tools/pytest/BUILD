load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "core_lib",
    srcs = [
        "core_lib.py",
        "pytest_main.py",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//third_party/python:pytest_core_lib",
        "@doipclient",
    ],
)

filegroup(
    name = "src",
    srcs = [
        "pytest_main.py",
        "qemu_menager.py",
    ],
    visibility = ["//visibility:public"],
)
