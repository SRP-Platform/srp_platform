load("@rules_python//python:defs.bzl", "py_library")
load("@pip_deps//:requirements.bzl", "requirement")


package(default_visibility = ["@srp_platform//visibility:public"])

py_library(
    name = "pyfranca",
    deps = [
        requirement("pyfranca"),
        requirement("ply"),
    ],
)
py_library(
    name = "someip_lib",
    deps = [requirement("someipy")],
    visibility = ["@srp_platform//visibility:public"],
)
