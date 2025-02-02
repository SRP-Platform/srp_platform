load("@rules_python//python:defs.bzl", "py_library")
load("@pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "pyfranca",
    deps = [
        requirement("pyfranca"),
        requirement("ply"),
    ],
)

py_library(
    name = "someip_lib",
    visibility = ["//visibility:public"],
    deps = [requirement("someipy")],
)

py_library(
    name = "pytest",
    visibility = ["//visibility:public"],
    deps = [
        requirement("pytest"),
        requirement("paramiko"),
    ],
)

py_library(
    name = "pytest_core_lib",
    visibility = ["//visibility:public"],
    deps = [
        requirement("paramiko"),
    ],
)
