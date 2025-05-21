load("@rules_python//python:defs.bzl", "py_library")
load("@pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "pyfranca",
    deps = [
        requirement("pyfranca"),
        requirement("ply"),
        requirement("jsonpickle"),
    ],
)

py_library(
    name = "pymavlink",
    deps = [
        requirement("fastcrc"),
        requirement("future"),
        requirement("h11"),
        requirement("iniconfig"),
        requirement("lxml"),
        requirement("packaging"),
        requirement("pluggy"),
        requirement("pymavlink"),
        requirement("pytest"),
        requirement("syrupy"),
        requirement("wheel"),
        requirement("wsproto"),
    ],
)
py_library(
    name = "requests",
    deps = [
        requirement("requests"),
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
